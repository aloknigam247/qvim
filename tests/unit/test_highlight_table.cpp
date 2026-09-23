#include <msgpack.hpp>
#include <QtTest>

#include "HighlightTable.h"

using namespace qvim;

namespace {

msgpack::object_handle packAttrMap(const std::vector<std::pair<std::string, int>> &intKeys,
                                   const std::vector<std::pair<std::string, bool>> &boolKeys) {
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_map(static_cast<uint32_t>(intKeys.size() + boolKeys.size()));
    for(const auto &[k, v]: intKeys) {
        pk.pack(k);
        pk.pack(v);
    }
    for(const auto &[k, v]: boolKeys) {
        pk.pack(k);
        pk.pack(v);
    }
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

// Packs the ext_hlstate `info` array as [ { "hi_name": <name> }, ... ].
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
        auto m = packAttrMap({ { "foreground", 0xff8800 }, { "background", 0x123456 } },
                             { { "bold", true }, { "italic", true } });
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
        auto m = packAttrMap({ { "special", 0x00ff00 } }, { { "undercurl", true } });
        h.defineAttr(3, m.get());
        HlAttr a = h.attr(3);
        QCOMPARE(a.sp, QColor(0x00, 0xff, 0x00));
        QVERIFY(a.undercurl);
    }

    void resolvedFallsBackToDefaults() {
        HighlightTable h;
        h.setDefaultColors(0xaaaaaa, 0x111111, 0xff0000);
        auto m = packAttrMap({}, { { "bold", true } });
        h.defineAttr(5, m.get());
        HlAttr a = h.resolved(5);
        QCOMPARE(a.fg, QColor(0xaa, 0xaa, 0xaa));
        QCOMPARE(a.bg, QColor(0x11, 0x11, 0x11));
        QVERIFY(a.bold);
    }

    void resolvedReverseSwapsFgBg() {
        HighlightTable h;
        h.setDefaultColors(0xaaaaaa, 0x111111, 0xff0000);
        auto m = packAttrMap({ { "foreground", 0x222222 }, { "background", 0x888888 } },
                             { { "reverse", true } });
        h.defineAttr(6, m.get());
        HlAttr a = h.resolved(6);
        QCOMPARE(a.fg, QColor(0x88, 0x88, 0x88));
        QCOMPARE(a.bg, QColor(0x22, 0x22, 0x22));
    }

    void getIntHandlesNegativeAndNonIntValues() {
        // A negative blend exercises the NEGATIVE_INTEGER arm; a boolean under an
        // integer key exercises the "present but wrong type -> default" arm.
        HighlightTable h;
        auto m = packAttrMap({ { "blend", -5 } }, { { "background", true } });
        h.defineAttr(9, m.get());
        HlAttr a = h.attr(9);
        QCOMPARE(a.blend, -5);
        QVERIFY(!a.bg.isValid()); // background was not a usable integer
    }

    void setRoundedHighlightsRecomputesExistingAttrs() {
        // Defining the attr before any rounded set means it starts non-rounded;
        // a later setRoundedHighlights must walk the existing attrs and flip it.
        HighlightTable h;
        auto attrMap = packAttrMap({ { "background", 0x445566 } }, {});
        auto info = packInfoNames({ "Search" });
        h.defineAttr(4, attrMap.get(), &info.get());
        QVERIFY(!h.isRounded(4));
        h.setRoundedHighlights({ QStringLiteral("Search") });
        QVERIFY(h.isRounded(4));
    }
};

QTEST_GUILESS_MAIN(TestHighlightTable)
#include "test_highlight_table.moc"
