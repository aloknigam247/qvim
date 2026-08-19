#include <msgpack.hpp>
#include <QSignalSpy>
#include <QtTest>

#include "PopupMenuModel.h"

using namespace qvim;

namespace {

msgpack::object_handle packItems(const std::vector<std::array<std::string, 4>> &items) {
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_array(static_cast<uint32_t>(items.size()));
    for(const auto &it: items) {
        pk.pack_array(4);
        pk.pack(it[0]);
        pk.pack(it[1]);
        pk.pack(it[2]);
        pk.pack(it[3]);
    }
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

} // namespace

class TestPopupmenuSelect : public QObject {
    Q_OBJECT
private slots:
    void selectChangesIndexAndEmits() {
        PopupMenuModel m;
        auto items =
            packItems({ { "foo", "f", "", "" }, { "bar", "v", "", "" }, { "baz", "m", "", "" } });
        m.show(items.get(), 0, 1, 2);

        QSignalSpy spy(&m, &PopupMenuModel::selectedChanged);
        QCOMPARE(m.selectedIndex(), 0);

        m.select(2);
        QCOMPARE(m.selectedIndex(), 2);
        QCOMPARE(spy.count(), 1);

        m.select(1);
        QCOMPARE(m.selectedIndex(), 1);
        QCOMPARE(spy.count(), 2);
    }

    void selectSameIndexIsNoOp() {
        PopupMenuModel m;
        auto items = packItems({ { "foo", "f", "", "" }, { "bar", "v", "", "" } });
        m.show(items.get(), 1, 0, 0);

        QSignalSpy spy(&m, &PopupMenuModel::selectedChanged);
        m.select(1);
        QCOMPARE(m.selectedIndex(), 1);
        QCOMPARE(spy.count(), 0);

        m.select(1);
        QCOMPARE(spy.count(), 0);
    }

    void selectMinusOneClearsSelection() {
        PopupMenuModel m;
        auto items = packItems({ { "foo", "f", "", "" }, { "bar", "v", "", "" } });
        m.show(items.get(), 0, 0, 0);

        QSignalSpy spy(&m, &PopupMenuModel::selectedChanged);
        m.select(-1);
        QCOMPARE(m.selectedIndex(), -1);
        QCOMPARE(spy.count(), 1);

        // Re-selecting -1 should be a no-op.
        m.select(-1);
        QCOMPARE(spy.count(), 1);
    }

    void selectOutOfBoundsIsStoredVerbatim() {
        // PopupMenuModel::select() does not clamp — it stores whatever idx
        // the server sent. The view layer is responsible for bounds checks.
        PopupMenuModel m;
        auto items = packItems({ { "a", "", "", "" }, { "b", "", "", "" } });
        m.show(items.get(), 0, 0, 0);

        QSignalSpy spy(&m, &PopupMenuModel::selectedChanged);
        m.select(99);
        QCOMPARE(m.selectedIndex(), 99);
        QCOMPARE(spy.count(), 1);
    }

    void selectDoesNotAffectAnchorOrVisibility() {
        PopupMenuModel m;
        auto items = packItems({ { "a", "", "", "" }, { "b", "", "", "" } });
        m.show(items.get(), 0, 7, 11);

        QSignalSpy visSpy(&m, &PopupMenuModel::visibilityChanged);
        QSignalSpy anchorSpy(&m, &PopupMenuModel::anchorChanged);
        m.select(1);

        QVERIFY(m.visible());
        QCOMPARE(m.anchorRow(), 7);
        QCOMPARE(m.anchorCol(), 11);
        QCOMPARE(visSpy.count(), 0);
        QCOMPARE(anchorSpy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestPopupmenuSelect)
#include "test_popupmenu_select.moc"
