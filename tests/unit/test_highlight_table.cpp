#include <QtTest>
#include <msgpack.hpp>

#include "HighlightTable.h"

using namespace qvim;

namespace {

msgpack::object_handle packAttrMap(const std::vector<std::pair<std::string, int>>& intKeys,
                                    const std::vector<std::pair<std::string, bool>>& boolKeys) {
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_map(static_cast<uint32_t>(intKeys.size() + boolKeys.size()));
    for (const auto& [k, v] : intKeys)  { pk.pack(k); pk.pack(v); }
    for (const auto& [k, v] : boolKeys) { pk.pack(k); pk.pack(v); }
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

} // namespace

class TestHighlightTable : public QObject {
    Q_OBJECT
private slots:
    void defaultColorsSet() {
        HighlightTable h;
        h.setDefaultColors(0xff0000, 0x00ff00, 0x0000ff);
        QCOMPARE(h.defaultFg(), QColor(0xff, 0x00, 0x00));
        QCOMPARE(h.defaultBg(), QColor(0x00, 0xff, 0x00));
        QCOMPARE(h.defaultSp(), QColor(0x00, 0x00, 0xff));
    }

    void defineAttrFgBgBoldItalic() {
        HighlightTable h;
        auto m = packAttrMap(
            {{"foreground", 0xff8800}, {"background", 0x123456}},
            {{"bold", true}, {"italic", true}});
        h.defineAttr(7, m.get());
        HlAttr a = h.attr(7);
        QCOMPARE(a.fg, QColor(0xff, 0x88, 0x00));
        QCOMPARE(a.bg, QColor(0x12, 0x34, 0x56));
        QVERIFY(a.bold);
        QVERIFY(a.italic);
        QVERIFY(!a.underline);
    }

    void defineAttrUndercurlSp() {
        HighlightTable h;
        h.setDefaultColors(0xffffff, 0x000000, 0xff0000);
        auto m = packAttrMap({{"special", 0x00ff00}}, {{"undercurl", true}});
        h.defineAttr(3, m.get());
        HlAttr a = h.attr(3);
        QCOMPARE(a.sp, QColor(0x00, 0xff, 0x00));
        QVERIFY(a.undercurl);
    }

    void resolvedFallsBackToDefaults() {
        HighlightTable h;
        h.setDefaultColors(0xaaaaaa, 0x111111, 0xff0000);
        auto m = packAttrMap({}, {{"bold", true}});
        h.defineAttr(5, m.get());
        HlAttr a = h.resolved(5);
        QCOMPARE(a.fg, QColor(0xaa, 0xaa, 0xaa));
        QCOMPARE(a.bg, QColor(0x11, 0x11, 0x11));
        QVERIFY(a.bold);
    }

    void resolvedReverseSwapsFgBg() {
        HighlightTable h;
        h.setDefaultColors(0xaaaaaa, 0x111111, 0xff0000);
        auto m = packAttrMap({{"foreground", 0x222222}, {"background", 0x888888}}, {{"reverse", true}});
        h.defineAttr(6, m.get());
        HlAttr a = h.resolved(6);
        QCOMPARE(a.fg, QColor(0x88, 0x88, 0x88));
        QCOMPARE(a.bg, QColor(0x22, 0x22, 0x22));
    }
};

QTEST_GUILESS_MAIN(TestHighlightTable)
#include "test_highlight_table.moc"
