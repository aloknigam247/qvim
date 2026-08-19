#pragma once

#include "MsgpackRpc.h"
#include <msgpack.hpp>
#include <QObject>
#include <qqmlregistration.h>
#include <QString>

namespace qvim {

class NvimConnector;

class ClipboardBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ClipboardBridge(QObject *parent = nullptr);

    void attachTo(NvimConnector *conn);

public slots:
    void pasteFromClipboard();

private slots:
    void onCustomNotification(const qvim::Notification &note);

private:
    void installYankAutocmd();

    NvimConnector *m_conn = nullptr;
};

} // namespace qvim
