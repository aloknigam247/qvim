#include <QtTest>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QScreen>
#include <msgpack.hpp>

#include "GridItem.h"
#include "GridModel.h"
#include "HighlightTable.h"
#include "NvimConnector.h"

using namespace qvim;

namespace {

// Mirrors the helper in test_grid_model.cpp — builds a msgpack array of
// grid_line cells suitable for GridModel::applyLine.
msgpack::object_handle packGridLineCells(
    const std::vector<std::tuple<std::string, int, int>>& cells)
{
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_array(static_cast<uint32_t>(cells.size()));
    for (const auto& [text, hl, repeat] : cells) {
        if (repeat > 0 && hl >= 0)      { pk.pack_array(3); pk.pack(text); pk.pack(hl); pk.pack(repeat); }
        else if (hl >= 0)               { pk.pack_array(2); pk.pack(text); pk.pack(hl); }
        else                            { pk.pack_array(1); pk.pack(text); }
    }
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

// Returns true when the image contains at least two distinct pixel values —
// i.e. something other than a single flat fill was actually painted.
bool hasNonUniformContent(const QImage& img)
{
    if (img.isNull() || img.width() == 0 || img.height() == 0) return false;
    const QRgb first = img.pixel(0, 0);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (img.pixel(x, y) != first) return true;
        }
    }
    return false;
}

} // namespace

class TestHiDpiRendering : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (QGuiApplication::primaryScreen() == nullptr) {
            QSKIP("No primary screen available (headless without minimal QPA).");
        }
    }

    void rendersStablyAcrossDevicePixelRatios() {
        // Seed an offline connector — no RPC, just the model objects.
        NvimConnector conn;
        GridModel*      gm = conn.grid();
        HighlightTable* hl = conn.highlights();
        QVERIFY(gm != nullptr);
        QVERIFY(hl != nullptr);

        constexpr int kCols = 20;
        constexpr int kRows = 5;
        gm->resize(kCols, kRows);

        // High-contrast defaults so painted glyphs are guaranteed to differ
        // from the background regardless of font availability.
        hl->setDefaultColors(0xFFFFFF, 0x000000, 0xFF0000);

        // Fill every row with a repeating ASCII pattern. Using a printable
        // glyph ensures the renderer emits ink, not just background fills.
        for (int r = 0; r < kRows; ++r) {
            auto cells = packGridLineCells({{"M", 0, kCols}});
            gm->applyLine(r, 0, cells.get());
        }
        gm->setCursor(2, 7);

        GridItem item;
        item.setConnector(&conn);
        // Force a deterministic font so cell metrics don't depend on the
        // default substitution table of the host machine.
        item.setFontName(QStringLiteral("Courier New"));
        item.setFontSize(12.0);

        const qreal cw = item.cellWidth();
        const qreal ch = item.cellHeight();
        QVERIFY2(cw > 0.0, "cellWidth must be positive");
        QVERIFY2(ch > 0.0, "cellHeight must be positive");

        const QSizeF logicalSize(cw * kCols, ch * kRows);
        item.setWidth(logicalSize.width());
        item.setHeight(logicalSize.height());

        // Sanity: cursor logical rect is independent of DPR — it lives entirely
        // in cell-space, computed from the unchanging QFontMetricsF metrics.
        const QRectF expectedCursorRect(
            gm->cursorCol() * cw,
            gm->cursorRow() * ch,
            cw, ch);
        QVERIFY(expectedCursorRect.left()   >= 0.0);
        QVERIFY(expectedCursorRect.top()    >= 0.0);
        QVERIFY(expectedCursorRect.right()  <= logicalSize.width()  + 1e-6);
        QVERIFY(expectedCursorRect.bottom() <= logicalSize.height() + 1e-6);

        const qreal kDprs[] = { 1.0, 1.5, 2.0 };
        qreal cwFirst = 0.0;
        qreal chFirst = 0.0;

        for (const qreal dpr : kDprs) {
            const int pxW = static_cast<int>(logicalSize.width()  * dpr);
            const int pxH = static_cast<int>(logicalSize.height() * dpr);
            QVERIFY2(pxW > 0 && pxH > 0, "image dimensions must be positive");

            QImage image(pxW, pxH, QImage::Format_ARGB32);
            image.setDevicePixelRatio(dpr);
            image.fill(Qt::transparent);

            {
                QPainter p(&image);
                QVERIFY(p.isActive());
                item.paint(&p);
            }

            // Geometry invariant: cellWidth/cellHeight come from QFontMetricsF
            // in *logical* pixels and must not drift as DPR changes.
            if (cwFirst == 0.0) {
                cwFirst = item.cellWidth();
                chFirst = item.cellHeight();
            } else {
                QCOMPARE(item.cellWidth(),  cwFirst);
                QCOMPARE(item.cellHeight(), chFirst);
            }

            // Cursor logical position is independent of DPR.
            const QRectF cursorRectAfter(
                gm->cursorCol() * item.cellWidth(),
                gm->cursorRow() * item.cellHeight(),
                item.cellWidth(), item.cellHeight());
            QCOMPARE(cursorRectAfter, expectedCursorRect);

            // Output sanity: the painter actually wrote pixels.
            QVERIFY2(hasNonUniformContent(image),
                qPrintable(QStringLiteral("Painted image is uniform at DPR=%1").arg(dpr)));

            // Backing-store dimensions must scale with DPR.
            QCOMPARE(image.devicePixelRatio(), dpr);
            QCOMPARE(image.width(),  pxW);
            QCOMPARE(image.height(), pxH);
        }
    }
};

QTEST_MAIN(TestHiDpiRendering)
#include "test_hidpi_rendering.moc"
