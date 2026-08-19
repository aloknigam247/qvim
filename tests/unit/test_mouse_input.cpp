#include <QMouseEvent>
#include <QPointF>
#include <QtTest>

#include "InputHandler.h"

using namespace qvim;

namespace {
// Build a QMouseEvent matching what Qt's event system delivers for a given
// gesture. For QEvent::MouseMove, Qt sets button() == Qt::NoButton (no
// transition this event) while buttons() reports the currently-held set.
QMouseEvent makeEvent(QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers mods = Qt::NoModifier) {
    const QPointF localPos(5.0, 5.0);
    return QMouseEvent(type, localPos, localPos, button, buttons, mods);
}
} // namespace

class TestMouseInput : public QObject {
    Q_OBJECT
private slots:
    void leftDragDerivesButtonFromButtons() {
        // Real-world repro: during a drag Qt sends QEvent::MouseMove with
        // button()==NoButton, buttons()==LeftButton. Old code switched on
        // ev->button() and bailed, dropping every drag event.
        auto ev = makeEvent(QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
        const auto m = InputHandler::mouseFor(&ev, QEvent::MouseMove);
        QVERIFY(m.valid);
        QCOMPARE(m.button, QStringLiteral("left"));
        QCOMPARE(m.action, QStringLiteral("drag"));
    }

    void middleDragDerivesButtonFromButtons() {
        auto ev = makeEvent(QEvent::MouseMove, Qt::NoButton, Qt::MiddleButton);
        const auto m = InputHandler::mouseFor(&ev, QEvent::MouseMove);
        QVERIFY(m.valid);
        QCOMPARE(m.button, QStringLiteral("middle"));
        QCOMPARE(m.action, QStringLiteral("drag"));
    }

    void rightDragDerivesButtonFromButtons() {
        auto ev = makeEvent(QEvent::MouseMove, Qt::NoButton, Qt::RightButton);
        const auto m = InputHandler::mouseFor(&ev, QEvent::MouseMove);
        QVERIFY(m.valid);
        QCOMPARE(m.button, QStringLiteral("right"));
        QCOMPARE(m.action, QStringLiteral("drag"));
    }

    void leftPressStillWorks() {
        auto ev = makeEvent(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        const auto m = InputHandler::mouseFor(&ev, QEvent::MouseButtonPress);
        QVERIFY(m.valid);
        QCOMPARE(m.button, QStringLiteral("left"));
        QCOMPARE(m.action, QStringLiteral("press"));
    }

    void leftReleaseStillWorks() {
        // On release, ev->button() reports the button that just transitioned
        // (LeftButton) while ev->buttons() no longer includes it.
        auto ev = makeEvent(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        const auto m = InputHandler::mouseFor(&ev, QEvent::MouseButtonRelease);
        QVERIFY(m.valid);
        QCOMPARE(m.button, QStringLiteral("left"));
        QCOMPARE(m.action, QStringLiteral("release"));
    }

    void hoverIsRejected() {
        // QEvent::MouseMove with no button held = hover. Nvim has nothing
        // to do with this — we must NOT send a drag.
        auto ev = makeEvent(QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
        const auto m = InputHandler::mouseFor(&ev, QEvent::MouseMove);
        QVERIFY(!m.valid);
    }

    void shiftDragCarriesModifier() {
        auto ev = makeEvent(QEvent::MouseMove, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
        const auto m = InputHandler::mouseFor(&ev, QEvent::MouseMove);
        QVERIFY(m.valid);
        QCOMPARE(m.button, QStringLiteral("left"));
        QCOMPARE(m.action, QStringLiteral("drag"));
        QCOMPARE(m.modifier, QStringLiteral("S-"));
    }

    void multiButtonDragPicksLeft() {
        // If the user has both left and right held, prefer Left (matches the
        // priority documented on `:help nvim_input_mouse` for ambiguous state).
        auto ev = makeEvent(QEvent::MouseMove, Qt::NoButton, Qt::LeftButton | Qt::RightButton);
        const auto m = InputHandler::mouseFor(&ev, QEvent::MouseMove);
        QVERIFY(m.valid);
        QCOMPARE(m.button, QStringLiteral("left"));
    }
};

QTEST_GUILESS_MAIN(TestMouseInput)
#include "test_mouse_input.moc"
