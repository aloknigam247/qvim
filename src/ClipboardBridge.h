#pragma once

#include <QObject>
#include <QString>
#include <qqmlregistration.h>
#include <msgpack.hpp>
#include "MsgpackRpc.h"

namespace qvim {

class NvimConnector;

class ClipboardBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ClipboardBridge(QObject* parent = nullptr);

    void attachTo(NvimConnector* conn);

public slots:
    void pasteFromClipboard();

private slots:
    void onCustomNotification(const QString& method, qvim::ObjectHandlePtr params);

private:
    void installYankAutocmd();

    NvimConnector* m_conn = nullptr;
};

} // namespace qvim
