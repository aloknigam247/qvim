#include <QtTest>
#include <msgpack.hpp>

#include "GridModel.h"

using namespace qvim;

namespace {

msgpack::object_handle packGridLineCells(const std::vector<std::tuple<std::string, int, int>>& cells) {
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

} // namespace

class TestGridModel : public QObject {
    Q_OBJECT
private slots:
    void resizeFillsCells() {
        GridModel g;
        g.resize(5, 2);
        QCOMPARE(g.cols(), 5);
        QCOMPARE(g.rows(), 2);
        QCOMPARE(g.cell(0, 0).text, QStringLiteral(" "));
        QCOMPARE(g.cell(1, 4).text, QStringLiteral(" "));
    }

    void applyLineBasic() {
        GridModel g;
        g.resize(5, 1);
        auto cells = packGridLineCells({{"a", 1, -1}, {"b", 1, -1}, {"c", 1, -1}});
        g.applyLine(0, 0, cells.get());
        QCOMPARE(g.cell(0, 0).text, QStringLiteral("a"));
        QCOMPARE(g.cell(0, 1).text, QStringLiteral("b"));
        QCOMPARE(g.cell(0, 2).text, QStringLiteral("c"));
        QCOMPARE(g.cell(0, 0).hlId, 1);
    }

    void applyLineWithRepeat() {
        GridModel g;
        g.resize(10, 1);
        auto cells = packGridLineCells({{"x", 2, 5}});
        g.applyLine(0, 2, cells.get());
        QCOMPARE(g.cell(0, 1).text, QStringLiteral(" "));
        for (int c = 2; c < 7; ++c) {
            QCOMPARE(g.cell(0, c).text, QStringLiteral("x"));
            QCOMPARE(g.cell(0, c).hlId, 2);
        }
        QCOMPARE(g.cell(0, 7).text, QStringLiteral(" "));
    }

    void applyLineOmittedHlReusesPrevious() {
        GridModel g;
        g.resize(5, 1);
        auto cells = packGridLineCells({{"a", 9, -1}, {"b", -1, -1}, {"c", -1, -1}});
        g.applyLine(0, 0, cells.get());
        QCOMPARE(g.cell(0, 0).hlId, 9);
        QCOMPARE(g.cell(0, 1).hlId, 9);
        QCOMPARE(g.cell(0, 2).hlId, 9);
    }

    void clearReturnsToSpaces() {
        GridModel g;
        g.resize(3, 1);
        auto cells = packGridLineCells({{"a", 1, -1}, {"b", 1, -1}, {"c", 1, -1}});
        g.applyLine(0, 0, cells.get());
        g.clear();
        for (int c = 0; c < 3; ++c) QCOMPARE(g.cell(0, c).text, QStringLiteral(" "));
    }

    void scrollUp() {
        GridModel g;
        g.resize(3, 3);
        auto row0 = packGridLineCells({{"a", 1, -1}, {"b", 1, -1}, {"c", 1, -1}});
        auto row1 = packGridLineCells({{"d", 1, -1}, {"e", 1, -1}, {"f", 1, -1}});
        auto row2 = packGridLineCells({{"g", 1, -1}, {"h", 1, -1}, {"i", 1, -1}});
        g.applyLine(0, 0, row0.get());
        g.applyLine(1, 0, row1.get());
        g.applyLine(2, 0, row2.get());
        g.scroll(0, 3, 0, 3, 1);  // scroll up by 1
        QCOMPARE(g.cell(0, 0).text, QStringLiteral("d"));
        QCOMPARE(g.cell(1, 0).text, QStringLiteral("g"));
    }

    void cursorSet() {
        GridModel g;
        g.resize(10, 5);
        g.setCursor(2, 3);
        QCOMPARE(g.cursorRow(), 2);
        QCOMPARE(g.cursorCol(), 3);
    }

    void dumpAsciiSnapshot() {
        GridModel g;
        g.resize(3, 2);
        auto row0 = packGridLineCells({{"a", 0, -1}, {"b", 0, -1}, {"c", 0, -1}});
        auto row1 = packGridLineCells({{"d", 0, -1}, {"e", 0, -1}, {"f", 0, -1}});
        g.applyLine(0, 0, row0.get());
        g.applyLine(1, 0, row1.get());
        QCOMPARE(g.dumpAscii(), QStringLiteral("abc\ndef\n"));
    }
};

QTEST_GUILESS_MAIN(TestGridModel)
#include "test_grid_model.moc"
