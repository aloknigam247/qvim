#include "WindowChrome.h"

#include <QQuickWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
// Older SDKs may not define these DWMWA attribute IDs; pin the values from the
// Microsoft docs so the build doesn't depend on a recent Windows SDK header.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#endif

namespace qvim {

WindowChrome::WindowChrome(QObject *parent) : QObject(parent) {}

void WindowChrome::applyToWindow([[maybe_unused]] QQuickWindow *window,
                                 [[maybe_unused]] QColor background) {
#ifdef Q_OS_WIN
    if(!window || !background.isValid()) return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if(!hwnd) return;

    const COLORREF caption = RGB(background.red(), background.green(), background.blue());
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));

    const QColor text = background.lightness() < 128 ? Qt::white : Qt::black;
    const COLORREF textRef = RGB(text.red(), text.green(), text.blue());
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textRef, sizeof(textRef));

    const BOOL dark = background.lightness() < 128 ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
#endif
}

void WindowChrome::activateOnShow(QQuickWindow *window) {
    if(!window) return;
    QObject::connect(window, &QQuickWindow::visibleChanged, this, [this, window]() {
        if(m_activated) return;
        if(!window->isVisible()) return;
        // Set the guard before activating: native show/activation can
        // synchronously re-dispatch visibleChanged, and a still-false guard
        // would let this slot recurse.
        m_activated = true;
        activateNow(window);
        emit activated();
    });
}

void WindowChrome::activateNow([[maybe_unused]] QQuickWindow *window) {
    if(!window) return;
    window->raise();
    window->requestActivate();
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    // Real native window only. Under the minimal QPA (used by tests) winId()
    // can return a nonzero non-HWND value; touching the foreground thread's
    // input queue with it would attach to the real desktop, so bail first.
    if(!hwnd || !IsWindow(hwnd)) return;

    // Windows denies SetForegroundWindow to a process that isn't the current
    // foreground process. Briefly attach to the foreground thread's input
    // queue so the activation is honoured, then detach.
    const HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD selfThread = GetCurrentThreadId();
    const bool attached = fgThread && fgThread != selfThread &&
                          AttachThreadInput(selfThread, fgThread, TRUE);

    ShowWindow(hwnd, SW_SHOW);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);

    if(attached) AttachThreadInput(selfThread, fgThread, FALSE);
#endif
}

} // namespace qvim
