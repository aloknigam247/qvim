#include <QSignalSpy>
#include <QtTest>
#include <msgpack.hpp>

#include "CmdlineModel.h"

using namespace qvim;

namespace {

// Pack a cmdline content array: [ [attrs, text], [attrs, text], ... ]
msgpack::object_handle packContent(const std::vector<std::string>& chunks) {
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_array(static_cast<uint32_t>(chunks.size()));
    for (const auto& s : chunks) {
        pk.pack_array(2);
        pk.pack_map(0);    // empty attrs
        pk.pack(s);
    }
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

} // namespace

class TestCmdlinePos : public QObject {
    Q_OBJECT
private slots:
    void setPosUpdatesCursorAndEmits() {
        CmdlineModel m;
        auto content = packContent({"hello", " world"});
        m.show(content.get(), 0, ":", "", 0, 1);
        QCOMPARE(m.content(), QStringLiteral("hello world"));
        QCOMPARE(m.cursorPos(), 0);
        QCOMPARE(m.level(), 1);

        QSignalSpy spy(&m, &CmdlineModel::contentChanged);

        m.setPos(5, 1);
        QCOMPARE(m.cursorPos(), 5);
        QCOMPARE(m.level(), 1);
        QCOMPARE(spy.count(), 1);

        m.setPos(11, 1);
        QCOMPARE(m.cursorPos(), 11);
        QCOMPARE(spy.count(), 2);
    }

    void setPosToZeroEmits() {
        CmdlineModel m;
        auto content = packContent({"abc"});
        m.show(content.get(), 2, ":", "", 0, 1);

        QSignalSpy spy(&m, &CmdlineModel::contentChanged);
        m.setPos(0, 1);
        QCOMPARE(m.cursorPos(), 0);
        QCOMPARE(spy.count(), 1);
    }

    void setPosPreservesContent() {
        CmdlineModel m;
        auto content = packContent({"foo bar"});
        m.show(content.get(), 0, ":", "prompt", 2, 1);

        m.setPos(3, 1);
        QCOMPARE(m.content(), QStringLiteral("foo bar"));
        QCOMPARE(m.firstChar(), QStringLiteral(":"));
        QCOMPARE(m.prompt(), QStringLiteral("prompt"));
        QCOMPARE(m.indent(), 2);
        QVERIFY(m.visible());
    }

    void setPosLevelMismatchDoesNotCorruptContent() {
        // A setPos for a different (nested) level should not blow away the
        // content string. Current behaviour is to record the new level/pos
        // and emit contentChanged; content stays intact so the view can react.
        CmdlineModel m;
        auto content = packContent({"outer"});
        m.show(content.get(), 1, ":", "", 0, 1);
        QCOMPARE(m.level(), 1);

        QSignalSpy spy(&m, &CmdlineModel::contentChanged);
        m.setPos(3, 2);   // level 2 while content belongs to level 1
        QCOMPARE(m.cursorPos(), 3);
        QCOMPARE(m.level(), 2);
        QCOMPARE(m.content(), QStringLiteral("outer"));   // unchanged
        QVERIFY(m.visible());
        QCOMPARE(spy.count(), 1);
    }

    void setPosBeforeShowStillRecordsValues() {
        // Pathological ordering — guard against state corruption if a
        // cmdline_pos arrives before any cmdline_show.
        CmdlineModel m;
        QCOMPARE(m.cursorPos(), 0);
        QCOMPARE(m.level(), 0);

        QSignalSpy spy(&m, &CmdlineModel::contentChanged);
        m.setPos(4, 1);
        QCOMPARE(m.cursorPos(), 4);
        QCOMPARE(m.level(), 1);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!m.visible());
        QVERIFY(m.content().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCmdlinePos)
#include "test_cmdline_pos.moc"
