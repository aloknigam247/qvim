// Tier 3 pixel-snapshot test.
//
// Renders a deterministically-seeded GridItem into a QImage and compares the
// result against a golden image stored in tests/golden/. The first run will
// produce the golden image and pass with a warning; subsequent runs assert
// the rendered output is pixel-identical (within a tiny tolerance to absorb
// font-rasterizer AA jitter across builds).
//
// Golden format note: the vcpkg-built Qt 6.10 in this repo ships only the
// built-in BMP/PBM/PGM/PPM/XBM/XPM image writers (no qpng plugin, libpng
// disabled). BMP is lossless and trivially deterministic so we use it as
// the golden format. If the Qt build later gains PNG support, switching
// the goldenPath suffix and writer format string is a one-line change.
//
// Determinism knobs:
//   - QGuiApplication is constructed with QT_QPA_PLATFORM=minimal (set by
//     CTest), QT_SCALE_FACTOR=1, QT_AUTO_SCREEN_SCALE_FACTOR=0.
//   - The QImage backing store is created at DPR=1.0 explicitly.
//   - The font family is fixed and the point size is fixed.
//   - HighlightTable default colors and a small attr table are seeded by hand.
//
// Font tradeoff
// =============
// Ideally we would bundle a permissively-licensed monospace TTF under
// tests/fonts/Test-Regular.ttf and load it with QFontDatabase. That would
// make the golden image stable across every Windows host. The agent
// executing this change had no network access to fetch such a font, and the
// monospace fonts shipping in C:\Windows\Fonts (Consolas, Cascadia Mono,
// Courier New, etc.) are not redistributable from a project repo.
//
// As a fallback we use "Courier New", which is present on every supported
// version of Windows. The minor cost is that the golden may need to be
// regenerated if the OS-shipped Courier New ever changes its metrics or
// rasterizer output. Delete tests/golden/grid_basic.bmp to regenerate.
//
// If a bundled TTF is later placed at tests/fonts/Test-Regular.ttf, this
// test will load it preferentially and prefer that family name.

#include <msgpack.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QString>
#include <QtTest>

#include "support/QuickRasterizer.h"

#include "GridItem.h"
#include "GridModel.h"
#include "HighlightTable.h"
#include "NvimConnector.h"

using namespace qvim;

namespace {

// Same helper used by other tests: packs a Neovim grid_line cells array.
msgpack::object_handle
    packGridLineCells(const std::vector<std::tuple<std::string, int, int>> &cells) {
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_array(static_cast<uint32_t>(cells.size()));
    for(const auto &[text, hl, repeat]: cells) {
        if(repeat > 0 && hl >= 0) {
            pk.pack_array(3);
            pk.pack(text);
            pk.pack(hl);
            pk.pack(repeat);
        } else if(hl >= 0) {
            pk.pack_array(2);
            pk.pack(text);
            pk.pack(hl);
        } else {
            pk.pack_array(1);
            pk.pack(text);
        }
    }
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

// Pack a single highlight attribute as the msgpack map shape that
// HighlightTable::defineAttr expects.
msgpack::object_handle packAttr(int fgRgb, int bgRgb, bool bold, bool italic) {
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    int n = 0;
    if(fgRgb >= 0) ++n;
    if(bgRgb >= 0) ++n;
    if(bold) ++n;
    if(italic) ++n;
    pk.pack_map(static_cast<uint32_t>(n));
    if(fgRgb >= 0) {
        pk.pack(std::string("foreground"));
        pk.pack(fgRgb);
    }
    if(bgRgb >= 0) {
        pk.pack(std::string("background"));
        pk.pack(bgRgb);
    }
    if(bold) {
        pk.pack(std::string("bold"));
        pk.pack(true);
    }
    if(italic) {
        pk.pack(std::string("italic"));
        pk.pack(true);
    }
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

// Per-channel absolute difference between two same-sized images.
// Returns the count of pixels whose max RGB delta exceeds tol.
qsizetype pixelDiffCount(const QImage &a, const QImage &b, int tol) {
    if(a.size() != b.size()) return std::numeric_limits<qsizetype>::max();
    QImage ax = a.convertToFormat(QImage::Format_ARGB32);
    QImage bx = b.convertToFormat(QImage::Format_ARGB32);
    qsizetype bad = 0;
    for(int y = 0; y < ax.height(); ++y) {
        const QRgb *ar = reinterpret_cast<const QRgb *>(ax.constScanLine(y));
        const QRgb *br = reinterpret_cast<const QRgb *>(bx.constScanLine(y));
        for(int x = 0; x < ax.width(); ++x) {
            const QRgb p = ar[x];
            const QRgb q = br[x];
            const int dr = std::abs(qRed(p) - qRed(q));
            const int dg = std::abs(qGreen(p) - qGreen(q));
            const int db = std::abs(qBlue(p) - qBlue(q));
            const int da = std::abs(qAlpha(p) - qAlpha(q));
            if(std::max({ dr, dg, db, da }) > tol) ++bad;
        }
    }
    return bad;
}

} // namespace

class TestPixelSnapshot : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        // The golden is compared pixel-for-pixel, so the renderer backend must
        // not vary by machine. Without this the RHI/D3D11 backend is picked
        // wherever it is available and the software rasteriser elsewhere,
        // producing a diff that looks like a renderer regression but is only
        // backend selection. Must run before any QQuickWindow is constructed.
        QuickRasterizer::useSoftwareBackend();

        // Pin platform-level scaling so the QImage backing store matches the
        // golden across machines. These env vars are also set by CTest, but
        // setting them belt-and-braces protects against ad-hoc runs.
        qputenv("QT_SCALE_FACTOR", "1");
        qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "0");

        // Ensure the PNG image-format plugin is discoverable. The CTest
        // environment exports QT_QPA_PLATFORM_PLUGIN_PATH for the platform
        // plugin, but not the parent plugins/ root that image-format plugins
        // (qjpeg, qpng-via-libqgif, etc.) live alongside. Walking up one
        // directory from the platforms/ folder finds plugins/ which is what
        // QCoreApplication::addLibraryPath expects.
        if(const QByteArray qpa = qgetenv("QT_QPA_PLATFORM_PLUGIN_PATH"); !qpa.isEmpty()) {
            const QString pluginsRoot = QFileInfo(QString::fromLocal8Bit(qpa)).absolutePath();
            QCoreApplication::addLibraryPath(pluginsRoot);
        }

        // Prefer a bundled TTF if present; fall back to Courier New otherwise.
        // The test's WORKING_DIRECTORY is tests/, so paths are relative there.
        const QString bundled = QStringLiteral("fonts/Test-Regular.ttf");
        if(QFileInfo::exists(bundled)) {
            const int id = QFontDatabase::addApplicationFont(bundled);
            QVERIFY2(id >= 0, "Failed to load bundled test font");
            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            QVERIFY(!families.isEmpty());
            m_fontFamily = families.first();
        } else {
            m_fontFamily = QStringLiteral("Courier New");
        }
    }

    void rendersDeterministicGrid() {
        // Build models directly through NvimConnector — no RPC, no nvim.
        NvimConnector conn;
        GridModel *gm = conn.grid();
        HighlightTable *hl = conn.highlights();
        QVERIFY(gm != nullptr);
        QVERIFY(hl != nullptr);

        constexpr int kCols = 20;
        constexpr int kRows = 5;
        gm->resize(kCols, kRows);

        // Lock the palette to fixed RGB values. defineAttr below references
        // these by id; resolved() will fall back to the defaults for
        // unspecified attributes.
        hl->setDefaultColors(/*fg*/ 0xD0D0D0, /*bg*/ 0x101820, /*sp*/ 0xFF5555);

        // Seed a small attribute table: id 1 = highlighted keyword, id 2 = comment.
        {
            auto a1 = packAttr(/*fg*/ 0x80C7FF, /*bg*/ -1, /*bold*/ true, /*italic*/ false);
            auto a2 = packAttr(/*fg*/ 0x7CA982, /*bg*/ -1, /*bold*/ false, /*italic*/ false);
            hl->defineAttr(1, a1.get());
            hl->defineAttr(2, a2.get());
        }

        // Deterministic content per row.
        const std::vector<std::tuple<std::string, int>> lines = {
            { "function hello() {  ", 1 }, { "  print('world!')   ", 0 },
            { "}                   ", 0 }, { "// a comment line   ", 2 },
            { "x = 1 + 2 + 3       ", 0 },
        };
        QCOMPARE(static_cast<int>(lines.size()), kRows);
        for(int r = 0; r < kRows; ++r) {
            const auto &[text, hlId] = lines[static_cast<size_t>(r)];
            QCOMPARE(static_cast<int>(text.size()), kCols);
            std::vector<std::tuple<std::string, int, int>> cells;
            cells.reserve(text.size());
            for(char ch: text) { cells.push_back({ std::string(1, ch), hlId, -1 }); }
            auto packed = packGridLineCells(cells);
            gm->applyLine(r, 0, packed.get());
        }
        gm->setCursor(2, 0);

        // Construct the GridItem and pin the font.
        GridItem item;
        item.setConnector(&conn);
        item.setFontName(m_fontFamily);
        item.setFontSize(12.0);

        const qreal cw = item.cellWidth();
        const qreal ch = item.cellHeight();
        QVERIFY2(cw > 0.0, "cellWidth must be positive");
        QVERIFY2(ch > 0.0, "cellHeight must be positive");

        // Force the geometry to an exact integer multiple of cell metrics so
        // the rendered output has integer pixel boundaries.
        const int pxW = static_cast<int>(std::ceil(cw * kCols));
        const int pxH = static_cast<int>(std::ceil(ch * kRows));
        item.setWidth(pxW);
        item.setHeight(pxH);

        // Render at DPR=1.0 through the real scene graph, offscreen via
        // QQuickRenderControl with the software adaptation. QQuickWindow::
        // grabWindow() is not usable here (it returns null under the minimal
        // QPA platform), and a test-only QPainter path would defeat the point
        // of a golden: it could stay stable while the shipping renderer broke.
        QuickRasterizer raster;
        QImage image = raster.render(&item).convertToFormat(QImage::Format_ARGB32);
        QVERIFY2(!image.isNull(), "scene graph produced no image");
        QVERIFY2(raster.isUnscaled(), "render surface is scaled; the golden is stored at 1:1");
        QCOMPARE(image.width(), pxW);
        QCOMPARE(image.height(), pxH);

        // Compare against the golden image; on first run, create it.
        const QString goldenPath = QStringLiteral("golden/grid_basic.bmp");
        QDir().mkpath(QStringLiteral("golden"));

        if(!QFile::exists(goldenPath)) {
            QImageWriter writer(goldenPath, "BMP");
            const bool ok = writer.write(image);
            QVERIFY2(ok, qPrintable(QStringLiteral(
                                        "Failed to write golden to %1: %2 (supported writers: %3)")
                                        .arg(goldenPath)
                                        .arg(writer.errorString())
                                        .arg(QString::fromLatin1(
                                            QImageWriter::supportedImageFormats().join(',')))));
            qWarning("Golden did not exist; wrote it now. Re-run the test to verify stability.");
            return;
        }

        QImage golden(goldenPath);
        QVERIFY2(!golden.isNull(),
                 qPrintable(QStringLiteral("Failed to load golden image %1").arg(goldenPath)));
        QCOMPARE(golden.size(), image.size());

        // Allow ≤2% of pixels to differ by up to 1 channel-step. This absorbs
        // the small amount of font-hinting jitter that QFontMetricsF cannot
        // promise to be byte-identical across Qt patch releases, while still
        // catching real regressions (which typically move thousands of pixels).
        constexpr int kChannelTol = 1;
        const qsizetype total = static_cast<qsizetype>(image.width()) * image.height();
        const qsizetype budget = (total * 2 + 99) / 100; // 2% rounded up
        const qsizetype bad = pixelDiffCount(image, golden, kChannelTol);

        if(bad > budget) {
            // Save the actual image alongside the golden for debugging.
            const QString diffPath = QStringLiteral("golden/grid_basic.actual.bmp");
            image.save(diffPath, "BMP");
            QFAIL(qPrintable(
                QStringLiteral("Pixel diff exceeded tolerance: %1 / %2 pixels differ (budget=%3). "
                               "Actual image written to %4.")
                    .arg(bad)
                    .arg(total)
                    .arg(budget)
                    .arg(diffPath)));
        }
    }

private:
    QString m_fontFamily;
};

QTEST_MAIN(TestPixelSnapshot)
#include "test_pixel_snapshot.moc"
