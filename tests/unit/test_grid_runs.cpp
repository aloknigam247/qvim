// Tier 1 — buildGridRuns, the grid traversal extracted from GridItem::paint().
//
// paint() used to walk GridModel inline, so none of this logic could be tested
// without a window and a painter. The traversal is now a pure function of
// (GridModel, HighlightTable, gridId), which lets these cases hard-assert the
// run decomposition directly.
//
// The cases that matter are the ones a pixel test cannot pin precisely:
//   - runs coalesce by hl_id and split at every boundary;
//   - double-width right halves contribute no glyph (they would otherwise
//     duplicate the wide glyph and shift the rest of the row);
//   - PUA cells are collected as clusters for the explicit-position pass,
//     because Qt's shaper gives them a zero advance and collapses them;
//   - pill spans sample the ambient background from a neighbour, which is what
//     stops rounded corners punching through to the default colour.

#include <msgpack.hpp>
#include <QGuiApplication>
#include <QtTest>

#include "GridModel.h"
#include "GridRuns.h"
#include "HighlightTable.h"

using namespace qvim;

namespace {

// Mirrors the helper in test_grid_model.cpp / test_rounded_highlight.cpp.
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

msgpack::object_handle packBgAttr(int bgRgb) {
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_map(1);
    pk.pack(std::string("background"));
    pk.pack(bgRgb);
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

// Packs the ext_hlstate `info` array: [ { "hi_name": <name> } ].
msgpack::object_handle packInfoNames(const std::vector<std::string> &names) {
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_array(static_cast<uint32_t>(names.size()));
    for(const auto &n: names) {
        pk.pack_map(1);
        pk.pack(std::string("hi_name"));
        pk.pack(n);
    }
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

void defineBg(HighlightTable &hl, int id, int bgRgb) {
    const auto attr = packBgAttr(bgRgb);
    hl.defineAttr(id, attr.get());
}

void defineBgNamed(HighlightTable &hl, int id, int bgRgb, const std::string &name) {
    const auto attr = packBgAttr(bgRgb);
    const auto info = packInfoNames({ name });
    hl.defineAttr(id, attr.get(), &info.get());
}

} // namespace

class TestGridRuns : public QObject {
    Q_OBJECT
private slots:

    // A row of one highlight must collapse to exactly one run, not one per cell.
    // Run coalescing is what keeps drawText calls proportional to highlight
    // changes rather than to column count.
    void coalescesRunsByHlId() {
        GridModel g;
        HighlightTable hl;
        g.resize(6, 1);
        auto cells = packGridLineCells({ { "a", 1, 6 } });
        g.applyLine(0, 0, cells.get());

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QCOMPARE(runs.runs.size(), 1);
        QCOMPARE(runs.runs[0].c0, 0);
        QCOMPARE(runs.runs[0].c1, 6);
        QCOMPARE(runs.runs[0].hlId, 1);
        QCOMPARE(runs.runs[0].text, QStringLiteral("aaaaaa"));
    }

    void splitsRunsAtHighlightBoundary() {
        GridModel g;
        HighlightTable hl;
        g.resize(4, 1);
        auto cells =
            packGridLineCells({ { "a", 1, -1 }, { "b", 1, -1 }, { "c", 2, -1 }, { "d", 2, -1 } });
        g.applyLine(0, 0, cells.get());

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QCOMPARE(runs.runs.size(), 2);
        QCOMPARE(runs.runs[0].hlId, 1);
        QCOMPARE(runs.runs[0].text, QStringLiteral("ab"));
        QCOMPARE(runs.runs[1].hlId, 2);
        QCOMPARE(runs.runs[1].text, QStringLiteral("cd"));
        // Runs must tile the row with no gap and no overlap.
        QCOMPARE(runs.runs[0].c1, runs.runs[1].c0);
    }

    // Rows are emitted top-to-bottom and runs left-to-right within a row. The
    // painter relies on this: a later run's background overpaints an earlier
    // run's glyph overflow, so reordering would change output.
    void emitsRunsInRowMajorOrder() {
        GridModel g;
        HighlightTable hl;
        g.resize(2, 3);
        for(int r = 0; r < 3; ++r) {
            auto cells = packGridLineCells({ { "x", r + 1, -1 }, { "y", r + 1, -1 } });
            g.applyLine(r, 0, cells.get());
        }

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QCOMPARE(runs.runs.size(), 3);
        for(int i = 0; i < 3; ++i) QCOMPARE(runs.runs[i].row, i);
    }

    // The right half of a double-width glyph is a marker cell: the left cell's
    // glyph already spans both columns, so emitting it again would double-draw
    // and push the remainder of the run out of alignment.
    void doubleWidthRightHalfContributesNoGlyph() {
        GridModel g;
        HighlightTable hl;
        g.resize(3, 1);
        // A wide CJK glyph followed by its empty right-half marker.
        auto cells =
            packGridLineCells({ { "\xe6\xbc\xa2", 1, -1 }, { "", 1, -1 }, { "z", 1, -1 } });
        g.applyLine(0, 0, cells.get());

        QVERIFY2(g.cell(0, 1).doubleWidth,
                 "fixture invalid: cell 1 is not flagged as a double-width right half");

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QCOMPARE(runs.runs.size(), 1);
        // Two glyphs across three columns, not three.
        QCOMPARE(runs.runs[0].text, QStringLiteral("\u6f22z"));
    }

    // Empty cells are spaces, so a run's glyph string stays positionally aligned
    // with its columns.
    void emptyCellsBecomeSpaces() {
        GridModel g;
        HighlightTable hl;
        g.resize(3, 1);
        const GridRuns runs = buildGridRuns(g, hl, 1);

        QCOMPARE(runs.runs.size(), 1);
        QCOMPARE(runs.runs[0].text, QStringLiteral("   "));
    }

    void detectsPrivateUseAreaCodepoints() {
        QVERIFY(isPuaChar(QChar(0xE000))); // first BMP PUA
        QVERIFY(isPuaChar(QChar(0xE0B0))); // powerline separator
        QVERIFY(isPuaChar(QChar(0xF8FF))); // last BMP PUA
        QVERIFY(isPuaChar(QChar(0xDB80))); // high surrogate, supplementary PUA-A
        QVERIFY(isPuaChar(QChar(0xDBFF))); // high surrogate, supplementary PUA-B

        QVERIFY(!isPuaChar(QChar('a')));
        QVERIFY(!isPuaChar(QChar(0xDFFF))); // above BMP PUA, below PUA-A
        QVERIFY(!isPuaChar(QChar(0x6F22))); // CJK, not PUA
        // U+1F600 is a surrogate pair whose high half is U+D83D — below the
        // U+DB80 PUA-A floor, so emoji must NOT be diverted to the PUA pass.
        // Diverting it would bypass Qt's font fallback and render tofu.
        QVERIFY(!isPuaChar(QString::fromUcs4(U"\U0001F600").at(0)));
    }

    // PUA cells need their own pass because Qt's shaper gives them a zero
    // advance; laid out as a run they collapse on top of each other.
    void collectsPuaCellsAsClusters() {
        GridModel g;
        HighlightTable hl;
        g.resize(4, 1);
        // U+E0B0 U+E0B6, then plain text.
        auto cells = packGridLineCells({ { "\xee\x82\xb0", 1, -1 },
                                         { "\xee\x82\xb6", 1, -1 },
                                         { "a", 1, -1 },
                                         { "b", 1, -1 } });
        g.applyLine(0, 0, cells.get());

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QCOMPARE(runs.puaClusters.size(), 1);
        QCOMPARE(runs.puaClusters[0].row, 0);
        QCOMPARE(runs.puaClusters[0].c0, 0);
        QCOMPARE(runs.puaClusters[0].c1, 2);
        QCOMPARE(runs.puaClusters[0].hlId, 1);
    }

    // PUA cells are drawn only by the cluster pass, so they must be blanked out
    // of the run text. If they were left in, correctness would hinge on an
    // unspecified Qt shaper behaviour: dropping PUA renders fine by accident,
    // while giving it a zero advance draws the icon twice and shifts every
    // later glyph in the run to the left.
    void puaCellsAreBlankedFromRunText() {
        GridModel g;
        HighlightTable hl;
        g.resize(4, 1);
        // U+E0B0, 'a', U+E0B6, 'b' — all one hl_id, so all one run.
        auto cells = packGridLineCells({ { "\xee\x82\xb0", 1, -1 },
                                         { "a", 1, -1 },
                                         { "\xee\x82\xb6", 1, -1 },
                                         { "b", 1, -1 } });
        g.applyLine(0, 0, cells.get());

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QCOMPARE(runs.runs.size(), 1);
        QCOMPARE(runs.runs[0].text, QStringLiteral(" a b"));
        // Non-PUA cells keep their true column, so the run text stays exactly
        // as wide as the run — the cluster pass supplies the icons.
        QCOMPARE(runs.runs[0].text.size(), 4);
        QCOMPARE(runs.puaClusters.size(), 2);
    }

    void splitsPuaClustersAtHighlightBoundary() {
        GridModel g;
        HighlightTable hl;
        g.resize(2, 1);
        auto cells = packGridLineCells({ { "\xee\x82\xb0", 1, -1 }, { "\xee\x82\xb6", 2, -1 } });
        g.applyLine(0, 0, cells.get());

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QCOMPARE(runs.puaClusters.size(), 2);
        QCOMPARE(runs.puaClusters[0].hlId, 1);
        QCOMPARE(runs.puaClusters[1].hlId, 2);
    }

    void ordinaryTextProducesNoPuaClusters() {
        GridModel g;
        HighlightTable hl;
        g.resize(4, 1);
        auto cells = packGridLineCells({ { "t", 1, 4 } });
        g.applyLine(0, 0, cells.get());

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QVERIFY(runs.puaClusters.isEmpty());
    }

    // A run whose background equals the protocol default must not be filled —
    // the whole-item clear already covers it, and filling per run would be
    // wasted work on every frame.
    void defaultBackgroundRunsAreNotFilled() {
        GridModel g;
        HighlightTable hl;
        hl.setDefaultColors(0xFFFFFF, 0x101010, 0xFF0000);
        defineBg(hl, 1, 0x101010); // same as default
        defineBg(hl, 2, 0x3060C0); // differs

        g.resize(2, 1);
        auto cells = packGridLineCells({ { "a", 1, -1 }, { "b", 2, -1 } });
        g.applyLine(0, 0, cells.get());

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QCOMPARE(runs.runs.size(), 2);
        QVERIFY(!runs.runs[0].fillBg);
        QVERIFY(runs.runs[1].fillBg);
    }

    // The pill's rounded corners are deliberately cut to reveal what is beneath.
    // The span must therefore carry the ambient background of its line, sampled
    // from a neighbour — otherwise the corners punch through to the default
    // colour (issue #17).
    void pillSpanInheritsAmbientBackgroundFromNeighbour() {
        GridModel g;
        HighlightTable hl;
        hl.setDefaultColors(0xFFFFFF, 0xE0E2EA, 0xFF0000);
        // setRoundedHighlights must precede defineAttr: isRounded is computed
        // at define time from the current rounded-name set.
        hl.setRoundedHighlights({ QStringLiteral("Search") });
        defineBgNamed(hl, 1, 0x3060C0, "CursorLine"); // ambient
        defineBgNamed(hl, 2, 0xFFD000, "Search");     // the pill

        g.resize(4, 1);
        auto cells =
            packGridLineCells({ { "a", 1, -1 }, { "b", 2, -1 }, { "c", 2, -1 }, { "d", 1, -1 } });
        g.applyLine(0, 0, cells.get());

        QVERIFY2(hl.isRounded(2), "fixture invalid: hl 2 did not resolve as rounded");

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QCOMPARE(runs.pills.size(), 1);
        QCOMPARE(runs.pills[0].c0, 1);
        QCOMPARE(runs.pills[0].c1, 3);
        QVERIFY2(runs.pills[0].backBg.isValid(),
                 "pill span must inherit an ambient background, else corners show the default");
        QCOMPARE(runs.pills[0].backBg, QColor(0x30, 0x60, 0xC0));
    }

    void noPillSpansWithoutRoundedHighlights() {
        GridModel g;
        HighlightTable hl;
        defineBg(hl, 1, 0x3060C0);
        g.resize(2, 1);
        auto cells = packGridLineCells({ { "a", 1, -1 }, { "b", 1, -1 } });
        g.applyLine(0, 0, cells.get());

        const GridRuns runs = buildGridRuns(g, hl, 1);

        QVERIFY(runs.pills.isEmpty());
    }

    // An unsized grid must yield nothing rather than indexing out of bounds.
    void emptyGridProducesNoRuns() {
        GridModel g;
        HighlightTable hl;
        const GridRuns runs = buildGridRuns(g, hl, 99);
        QVERIFY(runs.runs.isEmpty());
        QVERIFY(runs.pills.isEmpty());
        QVERIFY(runs.puaClusters.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestGridRuns)
#include "test_grid_runs.moc"
