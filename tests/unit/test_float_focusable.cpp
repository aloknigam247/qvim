// Unit-level coverage for the focusable bit threaded through win_float_pos.
// The Q_PROPERTY `isFocusable` on GridSurfaceProxy is what the QML float
// delegate binds to; this test pins the contract that:
//   - non-float grids are always focusable (independent of any prior float),
//   - setFloatPos(... focusable=true ...) sets the proxy true,
//   - setFloatPos(... focusable=false ...) sets the proxy false,
//   - the bit round-trips through gridIsFocusable() and the GridSurfaceProxy.

#include <QtTest>

#include "GridModel.h"

using namespace qvim;

class TestFloatFocusable : public QObject {
    Q_OBJECT
private slots:
    void nonFloatGridIsAlwaysFocusable() {
        GridModel g;
        g.resize(2, 10, 5);
        g.setPos(2, 0, 0, 10, 5);
        QVERIFY(g.gridIsFocusable(2));
        const auto* proxy = g.surfaceFor(2);
        QVERIFY(proxy);
        QVERIFY(proxy->isFocusable());
        QVERIFY(!proxy->isFloat());
    }

    void floatPosFocusableTrue() {
        GridModel g;
        g.resize(3, 10, 5);
        g.setFloatPos(3, /*anchorGrid*/ 1, /*row*/ 2, /*col*/ 3,
                      /*focusable*/ true, /*zindex*/ 50);
        QVERIFY(g.gridIsFloat(3));
        QVERIFY(g.gridIsFocusable(3));
        const auto* proxy = g.surfaceFor(3);
        QVERIFY(proxy);
        QVERIFY(proxy->isFloat());
        QVERIFY(proxy->isFocusable());
    }

    void floatPosFocusableFalse() {
        GridModel g;
        g.resize(4, 10, 5);
        g.setFloatPos(4, 1, 2, 3, /*focusable*/ false, /*zindex*/ 50);
        QVERIFY(g.gridIsFloat(4));
        QVERIFY(!g.gridIsFocusable(4));
        const auto* proxy = g.surfaceFor(4);
        QVERIFY(proxy);
        QVERIFY(proxy->isFloat());
        QVERIFY(!proxy->isFocusable());
    }

    // Flipping a float between focusable=true/false must drive the
    // focusableChanged signal so QML bindings re-evaluate without a destroy.
    void focusableChangedEmits() {
        GridModel g;
        g.resize(5, 10, 5);
        g.setFloatPos(5, 1, 2, 3, /*focusable*/ true, 50);
        auto* proxy = g.surfaceFor(5);
        QVERIFY(proxy);
        QSignalSpy spy(proxy, &GridSurfaceProxy::focusableChanged);
        g.setFloatPos(5, 1, 2, 3, /*focusable*/ false, 50);
        QCOMPARE(spy.count(), 1);
        g.setFloatPos(5, 1, 2, 3, /*focusable*/ false, 50);
        // No-op should not re-emit.
        QCOMPARE(spy.count(), 1);
    }

    // setPos must reset focusable to true: when nvim converts a previously
    // floating window back to a normal split it sends win_pos, not
    // win_float_pos, and the GUI must stop suppressing input on that grid.
    void setPosResetsFocusableToTrue() {
        GridModel g;
        g.resize(6, 10, 5);
        g.setFloatPos(6, 1, 2, 3, /*focusable*/ false, 50);
        QVERIFY(!g.gridIsFocusable(6));
        g.setPos(6, 0, 0, 10, 5);
        QVERIFY(g.gridIsFocusable(6));
        QVERIFY(!g.gridIsFloat(6));
    }
};

QTEST_GUILESS_MAIN(TestFloatFocusable)
#include "test_float_focusable.moc"
