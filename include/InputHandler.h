#pragma once

#include <QKeyEvent>
#include <QMouseEvent>
#include <QString>
#include <Qt>

namespace qvim {

class InputHandler {
public:
    // Translate a Qt key event to a Neovim keycode string. Empty result means
    // "no input to send" (e.g., bare modifier press).
    static QString keyToNvim(QKeyEvent* ev);

    // Translate a mouse event into nvim_input_mouse arguments.
    struct MouseInput {
        QString button;
        QString action;
        QString modifier;
        bool    valid = false;
    };
    static MouseInput mouseFor(QMouseEvent* ev, QEvent::Type type);
    static QString wheelFor(int deltaY, Qt::KeyboardModifiers mods);

private:
    static QString modPrefix(Qt::KeyboardModifiers mods);
    static QString modString(Qt::KeyboardModifiers mods);
    static QString escapeLiteral(QChar c);
};

} // namespace qvim
