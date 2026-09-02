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

// Q_INVOKABLE: must stay a non-static member so moc can expose it to QML.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
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

} // namespace qvim
