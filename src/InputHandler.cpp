#include "InputHandler.h"

#include <QHash>

namespace qvim {

namespace {
const QHash<int, QString> &specialKeyMap() {
    static const QHash<int, QString> m = {
        { Qt::Key_Escape, "Esc" },
        { Qt::Key_Tab, "Tab" },
        { Qt::Key_Backtab, "Tab" },
        { Qt::Key_Backspace, "BS" },
        { Qt::Key_Return, "CR" },
        { Qt::Key_Enter, "CR" },
        { Qt::Key_Space, "Space" },
        { Qt::Key_Delete, "Del" },
        { Qt::Key_Insert, "Insert" },
        { Qt::Key_Home, "Home" },
        { Qt::Key_End, "End" },
        { Qt::Key_PageUp, "PageUp" },
        { Qt::Key_PageDown, "PageDown" },
        { Qt::Key_Up, "Up" },
        { Qt::Key_Down, "Down" },
        { Qt::Key_Left, "Left" },
        { Qt::Key_Right, "Right" },
        { Qt::Key_F1, "F1" },
        { Qt::Key_F2, "F2" },
        { Qt::Key_F3, "F3" },
        { Qt::Key_F4, "F4" },
        { Qt::Key_F5, "F5" },
        { Qt::Key_F6, "F6" },
        { Qt::Key_F7, "F7" },
        { Qt::Key_F8, "F8" },
        { Qt::Key_F9, "F9" },
        { Qt::Key_F10, "F10" },
        { Qt::Key_F11, "F11" },
        { Qt::Key_F12, "F12" },
    };
    return m;
}
} // namespace

QString InputHandler::modString(Qt::KeyboardModifiers mods) {
    QString s;
    if(mods & Qt::ShiftModifier) s += QStringLiteral("S-");
    if(mods & Qt::ControlModifier) s += QStringLiteral("C-");
    if(mods & Qt::AltModifier) s += QStringLiteral("M-");
    if(mods & Qt::MetaModifier) s += QStringLiteral("D-");
    return s;
}

QString InputHandler::modPrefix(Qt::KeyboardModifiers mods) {
    const QString m = modString(mods);
    return m.isEmpty() ? QString() : m;
}

QString InputHandler::escapeLiteral(QChar c) {
    if(c == QChar('<')) return QStringLiteral("<lt>");
    if(c == QChar('\\')) return QStringLiteral("\\");
    return QString(c);
}

QString InputHandler::keyToNvim(QKeyEvent *ev) {
    const int key = ev->key();
    Qt::KeyboardModifiers mods = ev->modifiers();

    // Ignore bare modifier presses.
    if(key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt ||
       key == Qt::Key_Meta || key == Qt::Key_CapsLock) {
        return {};
    }

    const auto &special = specialKeyMap();
    auto it = special.find(key);
    if(it != special.end()) {
        const QString name = it.value();
        return QStringLiteral("<%1%2>").arg(modString(mods), name);
    }

    QString text = ev->text();
    if(text.isEmpty()) return {};

    if(text.size() == 1) {
        const QChar c = text.at(0);

        // Control characters (< 0x20). Map Ctrl+letter to <C-letter>.
        if(c.unicode() < 0x20) {
            if((mods & Qt::ControlModifier) && key >= Qt::Key_A && key <= Qt::Key_Z) {
                const QChar letter = QChar('a' + (key - Qt::Key_A));
                // Shift is never encoded by a control character, so it must
                // survive into the prefix — otherwise <C-S-i> is
                // indistinguishable from <C-i> (which is also <Tab>).
                return QStringLiteral("<%1%2>").arg(modString(mods), letter);
            }
            return {};
        }

        if(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
            // Ctrl+Shift+<letter>: Qt may deliver the uppercase character, which
            // case-folds to the same keycode as plain Ctrl+letter, so Shift is
            // the only distinguishing information and must be kept. Alt/Meta are
            // excluded because AltGr (= Ctrl+Alt on Windows) legitimately
            // produces composed text that must not be replaced by the raw letter.
            const bool ctrlShiftLetter = (mods & Qt::ControlModifier) &&
                                         (mods & Qt::ShiftModifier) &&
                                         !(mods & (Qt::AltModifier | Qt::MetaModifier)) &&
                                         key >= Qt::Key_A && key <= Qt::Key_Z;

            Qt::KeyboardModifiers rest = mods;
            // Shift is already encoded by uppercase when text reflects it; strip it.
            if(!ctrlShiftLetter) rest &= ~Qt::ShiftModifier;
            // Inside a <...> token the angle brackets themselves must be
            // referred to by name, otherwise the keycode is ambiguous
            // (e.g. <C-<lt>> would nest brackets; <C->> would terminate early).
            QString keyPart;
            // Lowercase so both Qt deliveries of Ctrl+Shift+letter agree.
            if(ctrlShiftLetter) keyPart = QChar('a' + (key - Qt::Key_A));
            else if(c == QChar('<')) keyPart = QStringLiteral("lt");
            else if(c == QChar('>')) keyPart = QStringLiteral("gt");
            else if(c == QChar(' ')) keyPart = QStringLiteral("Space");
            else if(c == QChar('|')) keyPart = QStringLiteral("Bar");
            else keyPart = QString(c);
            return QStringLiteral("<%1%2>").arg(modString(rest), keyPart);
        }

        if(c == QChar('<')) return QStringLiteral("<lt>");
        return text;
    }

    return text;
}

InputHandler::MouseInput InputHandler::mouseFor(QMouseEvent *ev, QEvent::Type type) {
    MouseInput m;
    // Qt sets ev->button() only on press/release transitions; during a drag
    // (QEvent::MouseMove) it's Qt::NoButton and only ev->buttons() carries
    // the held set. Derive the logical button from the held set on drag,
    // otherwise the drag would silently drop on the floor and visual-mode
    // selection would only enter on mouse release.
    Qt::MouseButton btn = Qt::NoButton;
    if(type == QEvent::MouseMove) {
        const Qt::MouseButtons held = ev->buttons();
        if(held & Qt::LeftButton) btn = Qt::LeftButton;
        else if(held & Qt::MiddleButton) btn = Qt::MiddleButton;
        else if(held & Qt::RightButton) btn = Qt::RightButton;
        else return m; // hover, nothing to send
    } else {
        btn = ev->button();
    }
    switch(btn) {
        case Qt::LeftButton:
            m.button = QStringLiteral("left");
            break;
        case Qt::RightButton:
            m.button = QStringLiteral("right");
            break;
        case Qt::MiddleButton:
            m.button = QStringLiteral("middle");
            break;
        default:
            return m;
    }
    switch(type) {
        case QEvent::MouseButtonPress:
            m.action = QStringLiteral("press");
            break;
        case QEvent::MouseButtonRelease:
            m.action = QStringLiteral("release");
            break;
        case QEvent::MouseMove:
            m.action = QStringLiteral("drag");
            break;
        default:
            return m;
    }
    QString mod;
    Qt::KeyboardModifiers mods = ev->modifiers();
    if(mods & Qt::ShiftModifier) mod += QStringLiteral("S-");
    if(mods & Qt::ControlModifier) mod += QStringLiteral("C-");
    if(mods & Qt::AltModifier) mod += QStringLiteral("M-");
    m.modifier = mod;
    m.valid = true;
    return m;
}

InputHandler::MouseInput InputHandler::wheelFor(int deltaX, int deltaY,
                                                Qt::KeyboardModifiers mods) {
    MouseInput m;
    if(deltaX == 0 && deltaY == 0) return m;
    m.button = QStringLiteral("wheel");
    // Widen before qAbs: qAbs(INT_MIN) is undefined and asserts in debug Qt.
    if(qAbs(static_cast<qint64>(deltaX)) > qAbs(static_cast<qint64>(deltaY))) {
        m.action = deltaX > 0 ? QStringLiteral("left") : QStringLiteral("right");
    } else {
        m.action = deltaY > 0 ? QStringLiteral("up") : QStringLiteral("down");
    }
    m.modifier = modString(mods);
    m.valid = true;
    return m;
}

} // namespace qvim
