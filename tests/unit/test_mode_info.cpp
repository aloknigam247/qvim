#include <msgpack.hpp>
#include <QtTest>

#include "ModeInfo.h"

using namespace qvim;

class TestModeInfo : public QObject {
    Q_OBJECT
private slots:
    // One mode whose fields deliberately exercise the type-mismatch arms of the
    // map readers: a non-string name, a negative integer, and an integer key
    // carrying a string value (which must fall back to the default).
    void setModesParsesFieldsAndTypeMismatches() {
        msgpack::sbuffer buf;
        msgpack::packer<msgpack::sbuffer> pk(&buf);
        pk.pack_array(1);
        pk.pack_map(5);
        pk.pack(std::string("name"));
        pk.pack(42); // non-string name -> empty, triggers the setCurrentMode fallback
        pk.pack(std::string("cursor_shape"));
        pk.pack(std::string("vertical"));
        pk.pack(std::string("cell_percentage"));
        pk.pack(-1); // negative integer arm
        pk.pack(std::string("blinkon"));
        pk.pack(std::string("nope")); // wrong type -> default (0)
        pk.pack(std::string("blinkwait"));
        pk.pack(250);

        msgpack::object_handle h;
        msgpack::unpack(h, buf.data(), buf.size());

        ModeInfo mi;
        QSignalSpy spy(&mi, &ModeInfo::currentChanged);
        mi.setModes(h.get(), true);
        mi.setCurrentMode(QStringLiteral("insert"), 0);

        QVERIFY(mi.cursorStyleEnabled());
        QCOMPARE(mi.currentName(), QStringLiteral("insert")); // fell back to the passed name
        QCOMPARE(mi.cursorShapeInt(), static_cast<int>(CursorShape::Vertical));
        QCOMPARE(mi.cellPercentage(), -1);
        QCOMPARE(mi.blinkOn(), 0);
        QCOMPARE(mi.blinkWait(), 250);
        QVERIFY(spy.count() >= 1);
    }

    void setCurrentModeOutOfRangeUsesNameOnly() {
        ModeInfo mi;
        // No modes defined: any index is out of range, so the descriptor resets
        // to defaults and only the name is applied.
        mi.setCurrentMode(QStringLiteral("cmdline"), 99);
        QCOMPARE(mi.currentName(), QStringLiteral("cmdline"));
        QCOMPARE(mi.cellPercentage(), 100); // default descriptor
        QCOMPARE(mi.cursorShapeInt(), static_cast<int>(CursorShape::Block));
    }

    void setModesIgnoresNonArray() {
        msgpack::sbuffer buf;
        msgpack::packer<msgpack::sbuffer> pk(&buf);
        pk.pack(7); // not an array

        msgpack::object_handle h;
        msgpack::unpack(h, buf.data(), buf.size());

        ModeInfo mi;
        mi.setModes(h.get(), false);
        mi.setCurrentMode(QStringLiteral("normal"), 0);
        QCOMPARE(mi.currentName(), QStringLiteral("normal"));
    }
};

QTEST_GUILESS_MAIN(TestModeInfo)
#include "test_mode_info.moc"
