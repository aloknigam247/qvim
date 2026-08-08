// Tier 1 — rounded-highlight backing colour (issue #17).
//
// The rounded-highlight pass in GridItem::paint() draws a pill whose corners are
// cut (convex) or bitten (concave), deliberately revealing whatever is painted
// underneath. Historically the ONLY thing painted underneath was the whole-item
// default-background clear, because the run loop explicitly skips the per-cell
// background fill for rounded cells. So whenever the line's ambient background
// differed from the protocol default — a CursorLine, a Visual block, a plugin's
// line highlight — the pill's corners punched through to the default colour
// instead of the ambient one.
//
// These tests render a GridItem into a QImage and hard-assert the actual pixel
// colour at the pill's corner cutout. A HighlightTable::resolved() QCOMPARE
// cannot catch this class of bug: resolved() is already correct, the defect is
// purely in what paint() composites. Hence pixel probes.
//
// Every probe point is derived from the cell geometry rather than hardcoded, and
// each test first proves the probe actually landed in the cutout (i.e. is not
// the pill colour) before asserting what colour the cutout is. Without that
// guard a mis-aimed probe would pass silently.

#include <QtTest>
#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <msgpack.hpp>

#include "support/QuickRasterizer.h"

#include "GridItem.h"
#include "GridModel.h"
#include "HighlightTable.h"
#include "NvimConnector.h"

using namespace qvim;

namespace {

constexpr int kDefaultBgRgb = 0xE0E2EA;  // editor Normal background
constexpr int kAmbientBgRgb = 0x3060C0;  // e.g. CursorLine — the colour corners must show
constexpr int kPillBgRgb    = 0xFFD000;  // e.g. Search — the rounded highlight itself

constexpr int kAmbientHl = 1;
constexpr int kPillHl    = 2;

// Mirrors the helper in test_grid_model.cpp / test_hidpi_rendering.cpp.
msgpack::object_handle packGridLineCells(
    const std::vector<std::tuple<std::string, int, int>>& cells)
{
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_array(static_cast<uint32_t>(cells.size()));
    for (const auto& [text, hl, repeat] : cells) {
        if (repeat > 0 && hl >= 0) { pk.pack_array(3); pk.pack(text); pk.pack(hl); pk.pack(repeat); }
        else if (hl >= 0)          { pk.pack_array(2); pk.pack(text); pk.pack(hl); }
        else                       { pk.pack_array(1); pk.pack(text); }
    }
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

// Packs { "background": <rgb> } — the rgb_attr map shape of hl_attr_define.
msgpack::object_handle packBgAttr(int bgRgb)
{
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_map(1);
    pk.pack(std::string("background"));
    pk.pack(bgRgb);
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

// Packs the ext_hlstate `info` array: [ { "hi_name": <name> } ]. Going through
// this path rather than poking isRounded directly means the test also covers
// the 4-element hl_attr_define shape that ext_hlstate produces.
msgpack::object_handle packInfoNames(const std::vector<std::string>& names)
{
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_array(static_cast<uint32_t>(names.size()));
    for (const auto& n : names) {
        pk.pack_map(1);
        pk.pack(std::string("hi_name"));
        pk.pack(n);
    }
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

QString describe(QRgb c)
{
    return QStringLiteral("#%1").arg(QColor::fromRgb(c).rgb() & 0xFFFFFF, 6, 16, QLatin1Char('0'));
}

} // namespace

class TestRoundedHighlight : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Must precede any QQuickWindow: the scene graph backend is chosen once.
        QuickRasterizer::useSoftwareBackend();
        if (QGuiApplication::primaryScreen() == nullptr) {
            QSKIP("No primary screen available (headless without minimal QPA).");
        }
    }

    // Single-row pill with non-rounded neighbours on both sides. The corner
    // cutout must show the neighbours' ambient background, not defaultBg.
    void cornerShowsAmbientNotDefaultBackground() {
        Fixture f;
        f.buildSingleRowPill(/*ambientRgb*/ kAmbientBgRgb);
        const QImage img = f.render();

        const QPoint probe = f.cornerProbe();
        const QRgb   px    = img.pixel(probe);

        QVERIFY2(px != qRgb(0xFF, 0xD0, 0x00),
                 qPrintable(QStringLiteral(
                     "Probe (%1,%2) landed on the pill itself, not the corner cutout — "
                     "the test would pass vacuously. Got %3.")
                     .arg(probe.x()).arg(probe.y()).arg(describe(px))));

        QCOMPARE(px, qRgb(0x30, 0x60, 0xC0));
    }

    // The pill itself must still be drawn — guards against "fixing" the corner
    // by painting the backing colour over the whole span.
    void pillInteriorKeepsItsOwnColour() {
        Fixture f;
        f.buildSingleRowPill(/*ambientRgb*/ kAmbientBgRgb);
        const QImage img = f.render();

        QCOMPARE(img.pixel(f.pillCentreProbe()), qRgb(0xFF, 0xD0, 0x00));
    }

    // Ordinary (non-rounded) cells must be unaffected by the change.
    void nonRoundedCellsUnchanged() {
        Fixture f;
        f.buildSingleRowPill(/*ambientRgb*/ kAmbientBgRgb);
        const QImage img = f.render();

        QCOMPARE(img.pixel(f.ambientProbe()), qRgb(0x30, 0x60, 0xC0));
    }

    // When the neighbours carry the default background there is nothing to
    // inherit, so the corner must stay defaultBg. Proves the fix does not
    // invent a colour, and that plain-colorscheme rendering is untouched.
    void defaultBackgroundPreservedWhenNothingToInherit() {
        Fixture f;
        f.buildSingleRowPill(/*ambientRgb*/ kDefaultBgRgb);
        const QImage img = f.render();

        const QPoint probe = f.cornerProbe();
        const QRgb   px    = img.pixel(probe);

        QVERIFY2(px != qRgb(0xFF, 0xD0, 0x00),
                 qPrintable(QStringLiteral("Probe (%1,%2) landed on the pill, not the cutout.")
                     .arg(probe.x()).arg(probe.y())));

        QCOMPARE(px, qRgb(0xE0, 0xE2, 0xEA));
    }

    // Two-row group with a column step, producing a concave (reflex) vertex in
    // the union polygon. Rubber-duck review flagged this as a possible gap in
    // the per-span backing fill; this pins the behaviour instead of trusting a
    // geometry argument. No pixel in the neighbourhood of the step vertex may
    // read defaultBg.
    void concaveStepCornerNeverShowsDefaultBackground() {
        Fixture f;
        f.buildTwoRowSteppedPill(/*ambientRgb*/ kAmbientBgRgb);
        const QImage img = f.render();

        const QRect box = f.concaveStepProbeBox(img.rect());
        QVERIFY2(box.width() > 0 && box.height() > 0, "Empty concave-step probe box");

        int defaultBgHits = 0;
        QPoint firstHit;
        for (int y = box.top(); y <= box.bottom(); ++y) {
            for (int x = box.left(); x <= box.right(); ++x) {
                if (img.pixel(x, y) == qRgb(0xE0, 0xE2, 0xEA)) {
                    if (defaultBgHits == 0) firstHit = QPoint(x, y);
                    ++defaultBgHits;
                }
            }
        }

        QVERIFY2(defaultBgHits == 0,
                 qPrintable(QStringLiteral(
                     "%1 pixel(s) around the concave step vertex show defaultBg #E0E2EA "
                     "(first at %2,%3); expected ambient #3060C0 or pill #FFD000.")
                     .arg(defaultBgHits).arg(firstHit.x()).arg(firstHit.y())));
    }

private:
    // Owns the connector + item so each test starts from a clean grid.
    struct Fixture {
        NvimConnector conn;
        GridItem      item;
        int           cols = 0;
        int           rows = 0;
        // Column extents of the rounded span(s), row-indexed.
        int row0C0 = 0, row0C1 = 0;
        int row1C0 = 0, row1C1 = 0;
        bool twoRow = false;

        Fixture() {
            item.setConnector(&conn);
            item.setFontName(QStringLiteral("Courier New"));
            item.setFontSize(12.0);
        }

        HighlightTable* hl() { return conn.highlights(); }
        GridModel*      gm() { return conn.grid(); }

        qreal cw() const { return item.cellWidth();  }
        qreal ch() const { return item.cellHeight(); }

        // setRoundedHighlights MUST run before defineAttr: defineAttr computes
        // isRounded at define time from the current rounded-name set.
        void seedHighlights(int ambientRgb) {
            hl()->setDefaultColors(/*fg*/ 0x101010, /*bg*/ kDefaultBgRgb, /*sp*/ 0xFF0000);
            hl()->setRoundedHighlights({QStringLiteral("Search")});

            auto ambientAttr = packBgAttr(ambientRgb);
            auto ambientInfo = packInfoNames({"CursorLine"});
            hl()->defineAttr(kAmbientHl, ambientAttr.get(), &ambientInfo.get());

            auto pillAttr = packBgAttr(kPillBgRgb);
            auto pillInfo = packInfoNames({"Search"});
            hl()->defineAttr(kPillHl, pillAttr.get(), &pillInfo.get());

            QVERIFY(hl()->isRounded(kPillHl));
            QVERIFY(!hl()->isRounded(kAmbientHl));
        }

        // Row 0: ambient | pill [4,9) | ambient
        void buildSingleRowPill(int ambientRgb) {
            cols = 20; rows = 3;
            row0C0 = 4; row0C1 = 9;
            twoRow = false;

            seedHighlights(ambientRgb);
            gm()->resize(cols, rows);
            writeRow(0, {{0, row0C0, kAmbientHl},
                         {row0C0, row0C1, kPillHl},
                         {row0C1, cols, kAmbientHl}});
            for (int r = 1; r < rows; ++r) writeRow(r, {{0, cols, kAmbientHl}});
            sizeItem();
        }

        // Row 0: pill [2,9); Row 1: pill [4,9)  -> reflex vertex at (4*cw, 1*ch)
        void buildTwoRowSteppedPill(int ambientRgb) {
            cols = 20; rows = 4;
            row0C0 = 2; row0C1 = 9;
            row1C0 = 4; row1C1 = 9;
            twoRow = true;

            seedHighlights(ambientRgb);
            gm()->resize(cols, rows);
            writeRow(0, {{0, row0C0, kAmbientHl},
                         {row0C0, row0C1, kPillHl},
                         {row0C1, cols, kAmbientHl}});
            writeRow(1, {{0, row1C0, kAmbientHl},
                         {row1C0, row1C1, kPillHl},
                         {row1C1, cols, kAmbientHl}});
            for (int r = 2; r < rows; ++r) writeRow(r, {{0, cols, kAmbientHl}});
            sizeItem();
        }

        // Writes a row from [start, end) runs of a given hl id, using blanks so
        // no glyph ink can contaminate a background probe.
        void writeRow(int row, const std::vector<std::tuple<int, int, int>>& runs) {
            std::vector<std::tuple<std::string, int, int>> cells;
            for (const auto& [c0, c1, id] : runs) {
                if (c1 > c0) cells.push_back({" ", id, c1 - c0});
            }
            auto packed = packGridLineCells(cells);
            gm()->applyLine(row, 0, packed.get());
        }

        void sizeItem() {
            item.setWidth (std::ceil(cw() * cols));
            item.setHeight(std::ceil(ch() * rows));
        }

        // Renders through the real scene graph. Probing pixels produced by a
        // test-only QPainter path would let the shipping renderer break while
        // these assertions stayed green.
        QImage render() {
            const QImage img = raster.render(&item);
            if (!raster.isUnscaled())
                qFatal("render surface is scaled; probes assume 1 logical px == 1 device px");
            return img.convertToFormat(QImage::Format_RGB32);
        }

        QuickRasterizer raster;

        // Top-left corner of the pill's cell box. The rounded path cuts this
        // corner off, so this pixel is inside the rounded cell but outside the
        // pill — exactly the area the bug leaves showing defaultBg.
        QPoint cornerProbe() const {
            return QPoint(static_cast<int>(row0C0 * cw()),
                          static_cast<int>(0 * ch()));
        }

        QPoint pillCentreProbe() const {
            return QPoint(static_cast<int>((row0C0 + row0C1) / 2.0 * cw()),
                          static_cast<int>(0.5 * ch()));
        }

        QPoint ambientProbe() const {
            return QPoint(static_cast<int>(1 * cw()),
                          static_cast<int>(0.5 * ch()));
        }

        // A small box centred on the reflex vertex at (row1C0*cw, 1*ch).
        QRect concaveStepProbeBox(const QRect& bounds) const {
            const int vx = static_cast<int>(row1C0 * cw());
            const int vy = static_cast<int>(1 * ch());
            constexpr int kPad = 6;   // >= the pass's 5.0 corner radius
            return QRect(vx - kPad, vy - kPad, 2 * kPad, 2 * kPad).intersected(bounds);
        }
    };
};

QTEST_MAIN(TestRoundedHighlight)
#include "test_rounded_highlight.moc"
