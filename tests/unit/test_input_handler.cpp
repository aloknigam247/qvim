#include <QtTest>
#include <QKeyEvent>

#include "InputHandler.h"

using namespace qvim;

namespace {
QString translate(int key, Qt::KeyboardModifiers mods, const QString& text) {
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
        QCOMPARE(translate(Qt::Key_Shift,   Qt::ShiftModifier,   QString()), QString());
        QCOMPARE(translate(Qt::Key_Control, Qt::ControlModifier, QString()), QString());
        QCOMPARE(translate(Qt::Key_Alt,     Qt::AltModifier,     QString()), QString());
    }

    void arrowKey() {
        QCOMPARE(translate(Qt::Key_Right, Qt::NoModifier, QString()), QStringLiteral("<Right>"));
        QCOMPARE(translate(Qt::Key_Up, Qt::ControlModifier, QString()), QStringLiteral("<C-Up>"));
    }
};

QTEST_GUILESS_MAIN(TestInputHandler)
#include "test_input_handler.moc"
