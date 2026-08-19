#include <QKeyEvent>
#include <QtTest>

#include "InputHandler.h"

using namespace qvim;

namespace {
QString translate(int key, Qt::KeyboardModifiers mods, const QString &text) {
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    return InputHandler::keyToNvim(&ev);
}
} // namespace

class TestInputHandler : public QObject {
    Q_OBJECT
private slots:
    void plainLetter() {
        QCOMPARE(translate(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")), QStringLiteral("a"));
    }

    void shiftedLetter() {
        QCOMPARE(translate(Qt::Key_A, Qt::ShiftModifier, QStringLiteral("A")), QStringLiteral("A"));
    }

    void ctrlLetter() {
        QCOMPARE(translate(Qt::Key_C, Qt::ControlModifier, QStringLiteral("")),
                 QStringLiteral("<C-c>"));
    }

    void ctrlShiftLetter_controlCharDelivery() {
        // Qt delivers the control byte (0x09 for I) when Ctrl is held. Shift is
        // not encoded by that byte, so it must survive into the prefix.
        QCOMPARE(
            translate(Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("\x09")),
            QStringLiteral("<S-C-i>"));
    }

    void ctrlShiftLetter_uppercaseDelivery() {
        // The other possible Qt delivery: the uppercase character. It case-folds
        // to the same nvim keycode as plain Ctrl+letter, so Shift is again the
        // only distinguishing information. Both deliveries must agree.
        QCOMPARE(translate(Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("I")),
                 QStringLiteral("<S-C-i>"));
    }

    void ctrlShiftLetter_otherLetter() {
        QCOMPARE(
            translate(Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("\x18")),
            QStringLiteral("<S-C-x>"));
        QCOMPARE(translate(Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("X")),
                 QStringLiteral("<S-C-x>"));
    }

    void ctrlLetterWithoutShiftUnchanged() {
        QCOMPARE(translate(Qt::Key_I, Qt::ControlModifier, QStringLiteral("\x09")),
                 QStringLiteral("<C-i>"));
    }

    void shiftedLetterHasNoShiftPrefix() {
        // Shift is already carried by the uppercase character here.
        QCOMPARE(translate(Qt::Key_I, Qt::ShiftModifier, QStringLiteral("I")), QStringLiteral("I"));
    }

    void altGrComposedTextUnchanged() {
        // AltGr is Ctrl+Alt on Windows and produces composed text. The
        // Ctrl+Shift+letter exemption must not replace it with the raw key letter.
        QCOMPARE(
            translate(Qt::Key_E, Qt::ControlModifier | Qt::AltModifier, QStringLiteral("\u20AC")),
            QStringLiteral("<C-M-\u20AC>"));
    }

    void ctrlShiftTabUnchanged() {
        // Goes through the special-key map, not the letter branches.
        QCOMPARE(translate(Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier, QString()),
                 QStringLiteral("<S-C-Tab>"));
    }

    void altLetter() {
        QCOMPARE(translate(Qt::Key_F, Qt::AltModifier, QStringLiteral("f")),
                 QStringLiteral("<M-f>"));
    }

    void escapeKey() {
        QCOMPARE(translate(Qt::Key_Escape, Qt::NoModifier, QString()), QStringLiteral("<Esc>"));
    }

    void backspace() {
        QCOMPARE(translate(Qt::Key_Backspace, Qt::NoModifier, QString()), QStringLiteral("<BS>"));
    }

    void returnKey() {
        QCOMPARE(translate(Qt::Key_Return, Qt::NoModifier, QString()), QStringLiteral("<CR>"));
    }

    void functionKey() {
        QCOMPARE(translate(Qt::Key_F4, Qt::AltModifier, QString()), QStringLiteral("<M-F4>"));
    }

    void angleBracketLiteral() {
        QCOMPARE(translate(Qt::Key_Less, Qt::NoModifier, QStringLiteral("<")),
                 QStringLiteral("<lt>"));
    }

    void bareModifierIgnored() {
        QCOMPARE(translate(Qt::Key_Shift, Qt::ShiftModifier, QString()), QString());
        QCOMPARE(translate(Qt::Key_Control, Qt::ControlModifier, QString()), QString());
        QCOMPARE(translate(Qt::Key_Alt, Qt::AltModifier, QString()), QString());
    }

    void arrowKey() {
        QCOMPARE(translate(Qt::Key_Right, Qt::NoModifier, QString()), QStringLiteral("<Right>"));
        QCOMPARE(translate(Qt::Key_Up, Qt::ControlModifier, QString()), QStringLiteral("<C-Up>"));
    }

    // --- wheelFor -----------------------------------------------------------
    // Sign convention: positive deltaX == user scrolled LEFT (Qt negates the
    // Windows WM_MOUSEHWHEEL delta), and nvim's action "left" pans the view
    // left. Verified against Qt source, neovim-qt, Neovide, and a live nvim.

    void wheelVerticalUnchanged() {
        const auto up = InputHandler::wheelFor(0, 120, Qt::NoModifier);
        QVERIFY(up.valid);
        QCOMPARE(up.button, QStringLiteral("wheel"));
        QCOMPARE(up.action, QStringLiteral("up"));
        QCOMPARE(up.modifier, QString());
        QCOMPARE(InputHandler::wheelFor(0, -120, Qt::NoModifier).action, QStringLiteral("down"));
    }

    void wheelHorizontal() {
        QCOMPARE(InputHandler::wheelFor(120, 0, Qt::NoModifier).action, QStringLiteral("left"));
        QCOMPARE(InputHandler::wheelFor(-120, 0, Qt::NoModifier).action, QStringLiteral("right"));
    }

    void wheelDominantAxis() {
        QCOMPARE(InputHandler::wheelFor(10, 120, Qt::NoModifier).action, QStringLiteral("up"));
        QCOMPARE(InputHandler::wheelFor(10, -120, Qt::NoModifier).action, QStringLiteral("down"));
        QCOMPARE(InputHandler::wheelFor(120, 10, Qt::NoModifier).action, QStringLiteral("left"));
        QCOMPARE(InputHandler::wheelFor(-120, -10, Qt::NoModifier).action, QStringLiteral("right"));
        // Ties resolve to vertical, preserving pre-change behaviour.
        QCOMPARE(InputHandler::wheelFor(120, 120, Qt::NoModifier).action, QStringLiteral("up"));
        QCOMPARE(InputHandler::wheelFor(-120, 120, Qt::NoModifier).action, QStringLiteral("up"));
        QCOMPARE(InputHandler::wheelFor(120, -120, Qt::NoModifier).action, QStringLiteral("down"));
    }

    // Regression: nvim_input_mouse exact-matches the action against
    // up/down/left/right, so a modifier folded into the action string makes
    // nvim reject the event as "invalid button or action" and drop it.
    void wheelModifiersAreSeparate() {
        const auto s = InputHandler::wheelFor(120, 0, Qt::ShiftModifier);
        QCOMPARE(s.action, QStringLiteral("left")); // NOT "S-left"
        QCOMPARE(s.modifier, QStringLiteral("S-"));
        const auto c = InputHandler::wheelFor(0, 120, Qt::ControlModifier);
        QCOMPARE(c.action, QStringLiteral("up")); // NOT "C-up"
        QCOMPARE(c.modifier, QStringLiteral("C-"));
        const auto all = InputHandler::wheelFor(
            -120, 0, Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(all.action, QStringLiteral("right"));
        QCOMPARE(all.modifier, QStringLiteral("S-C-M-"));
    }

    void wheelZeroIgnored() {
        QVERIFY(!InputHandler::wheelFor(0, 0, Qt::NoModifier).valid);
        QVERIFY(!InputHandler::wheelFor(0, 0, Qt::ShiftModifier).valid); // no bare "S-"
    }

    void wheelExtremeDeltas() {
        // qAbs(INT_MIN) is UB, so wheelFor widens to qint64 before comparing.
        QCOMPARE(InputHandler::wheelFor(INT_MIN, 0, Qt::NoModifier).action,
                 QStringLiteral("right"));
        QCOMPARE(InputHandler::wheelFor(0, INT_MIN, Qt::NoModifier).action, QStringLiteral("down"));
        QCOMPARE(InputHandler::wheelFor(INT_MIN, 1, Qt::NoModifier).action,
                 QStringLiteral("right"));
    }
};

QTEST_GUILESS_MAIN(TestInputHandler)
#include "test_input_handler.moc"
