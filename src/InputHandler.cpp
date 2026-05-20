#include "InputHandler.h"

#include <QHash>

namespace qvim {

namespace {
const QHash<int, QString>& specialKeyMap() {
    static const QHash<int, QString> m = {
        {Qt::Key_Escape,    "Esc"},
        {Qt::Key_Tab,       "Tab"},
        {Qt::Key_Backtab,   "Tab"},
        {Qt::Key_Backspace, "BS"},
        {Qt::Key_Return,    "CR"},
        {Qt::Key_Enter,     "CR"},
        {Qt::Key_Space,     "Space"},
        {Qt::Key_Delete,    "Del"},
        {Qt::Key_Insert,    "Insert"},
        {Qt::Key_Home,      "Home"},
        {Qt::Key_End,       "End"},
        {Qt::Key_PageUp,    "PageUp"},
        {Qt::Key_PageDown,  "PageDown"},
        {Qt::Key_Up,        "Up"},
        {Qt::Key_Down,      "Down"},
        {Qt::Key_Left,      "Left"},
        {Qt::Key_Right,     "Right"},
        {Qt::Key_F1,  "F1"},  {Qt::Key_F2,  "F2"},  {Qt::Key_F3,  "F3"},
        {Qt::Key_F4,  "F4"},  {Qt::Key_F5,  "F5"},  {Qt::Key_F6,  "F6"},
        {Qt::Key_F7,  "F7"},  {Qt::Key_F8,  "F8"},  {Qt::Key_F9,  "F9"},
        {Qt::Key_F10, "F10"}, {Qt::Key_F11, "F11"}, {Qt::Key_F12, "F12"},
    };
    return m;
}
} // namespace

QString InputHandler::modString(Qt::KeyboardModifiers mods) {
    QString s;
    if (mods & Qt::ShiftModifier)   s += QStringLiteral("S-");
    if (mods & Qt::ControlModifier) s += QStringLiteral("C-");
    if (mods & Qt::AltModifier)     s += QStringLiteral("M-");
    if (mods & Qt::MetaModifier)    s += QStringLiteral("D-");
    return s;
}

QString InputHandler::modPrefix(Qt::KeyboardModifiers mods) {
    const QString m = modString(mods);
    return m.isEmpty() ? QString() : m;
}

QString InputHandler::escapeLiteral(QChar c) {
    if (c == QChar('<'))  return QStringLiteral("<lt>");
    if (c == QChar('\\')) return QStringLiteral("\\");
    return QString(c);
}

QString InputHandler::keyToNvim(QKeyEvent* ev) {
    const int key = ev->key();
    Qt::KeyboardModifiers mods = ev->modifiers();

    // Ignore bare modifier presses.
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt ||
        key == Qt::Key_Meta  || key == Qt::Key_CapsLock) {
        return {};
    }

    const auto& special = specialKeyMap();
    auto it = special.find(key);
    if (it != special.end()) {
        const QString name = it.value();
        return QStringLiteral("<%1%2>").arg(modString(mods), name);
    }

    QString text = ev->text();
    if (text.isEmpty()) return {};

    if (text.size() == 1) {
        const QChar c = text.at(0);

        // Control characters (< 0x20). Map Ctrl+letter to <C-letter>.
        if (c.unicode() < 0x20) {
            if ((mods & Qt::ControlModifier) && key >= Qt::Key_A && key <= Qt::Key_Z) {
                const QChar letter = QChar('a' + (key - Qt::Key_A));
                Qt::KeyboardModifiers rest = mods & ~Qt::ShiftModifier;
                return QStringLiteral("<%1%2>").arg(modString(rest), letter);
            }
            return {};
        }

        if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
            Qt::KeyboardModifiers rest = mods;
            // Shift is already encoded by uppercase when text reflects it; strip it.
            rest &= ~Qt::ShiftModifier;
            // Inside a <...> token the angle brackets themselves must be
            // referred to by name, otherwise the keycode is ambiguous
            // (e.g. <C-<lt>> would nest brackets; <C->> would terminate early).
            QString keyPart;
            if      (c == QChar('<'))  keyPart = QStringLiteral("lt");
            else if (c == QChar('>'))  keyPart = QStringLiteral("gt");
            else if (c == QChar(' '))  keyPart = QStringLiteral("Space");
            else if (c == QChar('|'))  keyPart = QStringLiteral("Bar");
            else                       keyPart = QString(c);
            return QStringLiteral("<%1%2>").arg(modString(rest), keyPart);
        }

        if (c == QChar('<')) return QStringLiteral("<lt>");
        return text;
    }

    return text;
}

InputHandler::MouseInput InputHandler::mouseFor(QMouseEvent* ev, QEvent::Type type) {
    MouseInput m;
    switch (ev->button()) {
        case Qt::LeftButton:   m.button = QStringLiteral("left"); break;
        case Qt::RightButton:  m.button = QStringLiteral("right"); break;
        case Qt::MiddleButton: m.button = QStringLiteral("middle"); break;
        default: return m;
    }
    switch (type) {
        case QEvent::MouseButtonPress:   m.action = QStringLiteral("press"); break;
        case QEvent::MouseButtonRelease: m.action = QStringLiteral("release"); break;
        case QEvent::MouseMove:          m.action = QStringLiteral("drag"); break;
        default: return m;
    }
    QString mod;
    Qt::KeyboardModifiers mods = ev->modifiers();
    if (mods & Qt::ShiftModifier)   mod += QStringLiteral("S-");
    if (mods & Qt::ControlModifier) mod += QStringLiteral("C-");
    if (mods & Qt::AltModifier)     mod += QStringLiteral("M-");
    m.modifier = mod;
    m.valid = true;
    return m;
}

QString InputHandler::wheelFor(int deltaY, Qt::KeyboardModifiers mods) {
    if (deltaY == 0) return {};
    QString mod;
    if (mods & Qt::ShiftModifier)   mod += QStringLiteral("S-");
    if (mods & Qt::ControlModifier) mod += QStringLiteral("C-");
    if (mods & Qt::AltModifier)     mod += QStringLiteral("M-");
    return mod + (deltaY > 0 ? QStringLiteral("up") : QStringLiteral("down"));
}

} // namespace qvim
