#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

#include <QKeyEvent>
#include <QMouseEvent>
#include <QString>
#include <Qt>

namespace qvim {

class InputHandler {
public:
    // Translate a Qt key event to a Neovim keycode string. Empty result means
    // "no input to send" (e.g., bare modifier press).
    static QString keyToNvim(QKeyEvent *ev);

    // Translate a mouse event into nvim_input_mouse arguments.
    struct MouseInput {
        QString button;
        QString action;
        QString modifier;
        bool valid = false;
    };
    static MouseInput mouseFor(QMouseEvent *ev, QEvent::Type type);

    // Translate a wheel event's angleDelta into nvim_input_mouse arguments.
    // The dominant axis wins; ties resolve to vertical. Returns an invalid
    // MouseInput when both deltas are zero.
    //
    // Sign convention: positive deltaX means the user scrolled LEFT. Qt negates
    // the Windows WM_MOUSEHWHEEL delta (which is positive-for-right) in
    // qwindowspointerhandler.cpp, and nvim's action "left" pans the view left.
    //
    // The modifier MUST stay a separate field: nvim_input_mouse exact-matches
    // the action against up/down/left/right, so folding a "S-" prefix into it
    // makes nvim reject the whole event as "invalid button or action".
    static MouseInput wheelFor(int deltaX, int deltaY, Qt::KeyboardModifiers mods);

private:
    static QString modPrefix(Qt::KeyboardModifiers mods);
    static QString modString(Qt::KeyboardModifiers mods);
    static QString escapeLiteral(QChar c);
};

} // namespace qvim

#endif
