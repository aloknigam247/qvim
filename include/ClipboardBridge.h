#ifndef CLIPBOARDBRIDGE_H
#define CLIPBOARDBRIDGE_H

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

    Q_SLOT void pasteFromClipboard();

private:
    Q_SLOT void onCustomNotification(const qvim::Notification &note);

    void installYankAutocmd();

    NvimConnector *m_conn = nullptr;
};

} // namespace qvim

#endif
