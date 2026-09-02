#ifndef WINDOWCHROME_H
#define WINDOWCHROME_H

#include <QColor>
#include <QObject>
#include <qqmlregistration.h>
#include <QQuickWindow>

namespace qvim {

class WindowChrome : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided via context property $windowChrome")

public:
    explicit WindowChrome(QObject *parent = nullptr);

    Q_INVOKABLE void applyToWindow(QQuickWindow *window, QColor background);

    // Install a once-only hook that brings the window to the foreground the
    // first time it becomes visible. qvim shows its window asynchronously long
    // after the launching shell has returned, so without this the OS leaves
    // whatever window was already foreground on top. Call once, before the
    // window is shown.
    Q_INVOKABLE void activateOnShow(QQuickWindow *window);

Q_SIGNALS:
    // Emitted exactly once, when the first-show activation runs.
    void activated();

private:
    static void activateNow(QQuickWindow *window);

    bool m_activated = false;
};

} // namespace qvim

#endif
