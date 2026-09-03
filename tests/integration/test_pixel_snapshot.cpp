// Tier 3 structural render test.
//
// Renders a deterministically-seeded GridItem into a QImage through the real
// software scene graph and asserts machine-portable structural properties of
// the result: the exact background fill, per-row glyph "ink" coverage and
// horizontal extent, and the colour family of each row's glyphs (which proves
// the highlight-attr -> colour mapping actually painted).
//
// Why not a pixel golden: a committed full-image golden is not portable across
// machines. Cell metrics are portable once the font is pinned (see below), but
// glyph anti-aliasing is not — the same grid rasterised via DirectWrite differs
// by ~12% of pixels between hosts, far past any tolerance that still catches a
// real regression. The structural assertions below survive AA (they classify
// glyph pixels by channel-mean ordering, which is preserved when AA blends a
// fixed foreground toward the dark background) while still failing if the grid
// is not painted or the highlight colours are wrong.
//
// Determinism knobs:
//   - QGuiApplication runs under the software scene-graph backend, with
//     QT_SCALE_FACTOR=1 and QT_AUTO_SCREEN_SCALE_FACTOR=0 (set by CTest and
//     belt-and-braces here) so one logical pixel is one device pixel.
//   - The QImage backing store is created at DPR=1.0 explicitly.
//   - The font family is fixed and the point size is fixed.
//   - HighlightTable default colors and a small attr table are seeded by hand.
//
// Font
// ====
// The grid is rasterised from a bundled, permissively-licensed monospace TTF at
// tests/fonts/Test-Regular.ttf (JetBrains Mono, SIL OFL 1.1 — see
// tests/fonts/OFL.txt) loaded via QFontDatabase. Pinning the font bytes keeps
// glyph metrics identical across hosts, so the image size and per-cell extents
// asserted below are stable regardless of which fonts the host has installed.

#include <msgpack.hpp>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QString>
#include <QtTest>

#include <algorithm>
#include <cmath>

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

// Aggregate stats over the "ink" pixels (glyph coverage) inside one horizontal
// band of the image. A pixel counts as ink when its max-channel distance from
// the background exceeds inkTol; edge/AA pixels near the background are ignored
// so the colour means below reflect glyph cores, not blended edges.
struct BandStats {
    qsizetype ink = 0;
    double meanR = 0.0, meanG = 0.0, meanB = 0.0;
    int minInkX = -1, maxInkX = -1;
};

BandStats analyzeBand(const QImage &img, int y0, int y1, QRgb bg, int inkTol) {
    BandStats s;
    const int br = qRed(bg), bgc = qGreen(bg), bb = qBlue(bg);
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    for(int y = y0; y < y1; ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for(int x = 0; x < img.width(); ++x) {
            const QRgb p = row[x];
            const int dr = std::abs(qRed(p) - br);
            const int dg = std::abs(qGreen(p) - bgc);
            const int db = std::abs(qBlue(p) - bb);
            if(std::max({ dr, dg, db }) <= inkTol) continue;
            ++s.ink;
            sumR += qRed(p);
            sumG += qGreen(p);
            sumB += qBlue(p);
            if(s.minInkX < 0 || x < s.minInkX) s.minInkX = x;
            if(x > s.maxInkX) s.maxInkX = x;
        }
    }
    if(s.ink > 0) {
        s.meanR = sumR / static_cast<double>(s.ink);
        s.meanG = sumG / static_cast<double>(s.ink);
        s.meanB = sumB / static_cast<double>(s.ink);
    }
    return s;
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

        // The golden is authored from the bundled font; a missing font must fail
        // loud, not silently fall back to a host font and reintroduce
        // machine-dependent metrics. WORKING_DIRECTORY is tests/, so the path is
        // relative there.
        const QString bundled = QStringLiteral("fonts/Test-Regular.ttf");
        QVERIFY2(QFileInfo::exists(bundled),
                 "Bundled test font tests/fonts/Test-Regular.ttf is missing");
        const int id = QFontDatabase::addApplicationFont(bundled);
        QVERIFY2(id >= 0, "Failed to load bundled test font");
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        QVERIFY(!families.isEmpty());
        m_fontFamily = families.first();
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
        // of the check: it could stay stable while the shipping renderer broke.
        QuickRasterizer raster;
        QImage image = raster.render(&item).convertToFormat(QImage::Format_ARGB32);
        QVERIFY2(!image.isNull(), "scene graph produced no image");
        QVERIFY2(raster.isUnscaled(), "render surface is scaled; the checks assume 1:1");
        QCOMPARE(image.width(), pxW);
        QCOMPARE(image.height(), pxH);

        // Structural assertions. These are portable across machines because they
        // pin the fixed colours we seeded (bg fill and per-attr fg) rather than
        // exact glyph anti-aliasing, which is host-dependent.
        const QRgb bg = qRgb(0x10, 0x18, 0x20);

        // 1. Background dominates. Cell interiors are a solid default-bg fill, so
        //    most of the image must sit within a couple of channel-steps of bg.
        {
            qsizetype nearBg = 0;
            const qsizetype total = static_cast<qsizetype>(pxW) * pxH;
            for(int y = 0; y < pxH; ++y) {
                const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
                for(int x = 0; x < pxW; ++x) {
                    const QRgb p = row[x];
                    const int d =
                        std::max({ std::abs(qRed(p) - qRed(bg)), std::abs(qGreen(p) - qGreen(bg)),
                                   std::abs(qBlue(p) - qBlue(bg)) });
                    if(d <= 6) ++nearBg;
                }
            }
            QVERIFY2(nearBg * 2 > total,
                     qPrintable(QStringLiteral("background does not dominate: %1 / %2 px near bg")
                                    .arg(nearBg)
                                    .arg(total)));
        }

        // 2. Per-row glyph ink and colour family. Bands follow the exact cell
        //    metrics; ink = pixels far enough from bg to be glyph cores.
        constexpr int kInkTol = 40;
        BandStats band[kRows];
        for(int r = 0; r < kRows; ++r) {
            const int y0 = std::clamp(qRound(r * ch), 0, pxH);
            const int y1 = std::clamp(qRound((r + 1) * ch), y0, pxH);
            band[r] = analyzeBand(image, y0, y1, bg, kInkTol);
            QVERIFY2(band[r].ink > 0,
                     qPrintable(QStringLiteral("row %1 painted no glyph ink").arg(r)));
        }

        auto blueScore = [](const BandStats &b) { return b.meanB - std::max(b.meanR, b.meanG); };
        auto greenScore = [](const BandStats &b) { return b.meanG - std::max(b.meanR, b.meanB); };
        auto graySpread = [](const BandStats &b) {
            return std::max({ b.meanR, b.meanG, b.meanB }) -
                   std::min({ b.meanR, b.meanG, b.meanB });
        };

        // Row 0 is attr 1 (bold blue 0x80C7FF): blue channel dominates.
        QVERIFY2(
            blueScore(band[0]) > 20.0,
            qPrintable(QStringLiteral("row 0 not blue: blueScore=%1").arg(blueScore(band[0]))));
        // Row 3 is attr 2 (green 0x7CA982): green channel dominates.
        QVERIFY2(
            greenScore(band[3]) > 15.0,
            qPrintable(QStringLiteral("row 3 not green: greenScore=%1").arg(greenScore(band[3]))));
        // Rows 1 and 4 are the default gray fg (0xD0D0D0): near-neutral, and
        // clearly neither the blue nor the green highlight.
        for(int r: { 1, 4 }) {
            QVERIFY2(graySpread(band[r]) < 35.0,
                     qPrintable(QStringLiteral("row %1 not neutral: graySpread=%2")
                                    .arg(r)
                                    .arg(graySpread(band[r]))));
            QVERIFY2(blueScore(band[r]) < 15.0 && greenScore(band[r]) < 12.0,
                     qPrintable(QStringLiteral("row %1 mis-tinted: blue=%2 green=%3")
                                    .arg(r)
                                    .arg(blueScore(band[r]))
                                    .arg(greenScore(band[r]))));
        }

        // 3. Horizontal extent distinguishes full-width rows from the sparse row.
        //    Rows 0 and 3 fill most of the grid; row 2 is a single glyph at the
        //    left, so its ink stays near the left edge and is far sparser.
        QVERIFY2(band[0].maxInkX > pxW * 0.6,
                 qPrintable(QStringLiteral("row 0 ink extent too short: maxInkX=%1 of %2")
                                .arg(band[0].maxInkX)
                                .arg(pxW)));
        QVERIFY2(band[3].maxInkX > pxW * 0.6,
                 qPrintable(QStringLiteral("row 3 ink extent too short: maxInkX=%1 of %2")
                                .arg(band[3].maxInkX)
                                .arg(pxW)));
        QVERIFY2(band[2].maxInkX < pxW * 0.25,
                 qPrintable(QStringLiteral("row 2 ink should hug the left: maxInkX=%1 of %2")
                                .arg(band[2].maxInkX)
                                .arg(pxW)));
        QVERIFY2(band[2].ink * 3 < band[0].ink,
                 qPrintable(QStringLiteral("row 2 not sparse vs row 0: %1 vs %2")
                                .arg(band[2].ink)
                                .arg(band[0].ink)));
    }

private:
    QString m_fontFamily;
};

QTEST_MAIN(TestPixelSnapshot)
#include "test_pixel_snapshot.moc"
