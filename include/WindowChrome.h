#pragma once

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
};

} // namespace qvim
