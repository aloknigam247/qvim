#pragma once

#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QtTest>

#include "NvimConnector.h"
#include "GridModel.h"
#include "TablineModel.h"
#include "PopupMenuModel.h"
#include "CmdlineModel.h"

namespace qvim::test {

inline QString locateNvim() {
    const QString p = QStandardPaths::findExecutable(QStringLiteral("nvim"));
    return p.isEmpty() ? QStringLiteral("nvim") : p;
}

inline bool waitForFlush(NvimConnector* conn, int timeoutMs = 5000) {
    QSignalSpy spy(conn, &NvimConnector::flush);
    return spy.wait(timeoutMs);
}

inline bool waitForAttach(NvimConnector* conn, int timeoutMs = 5000) {
    if (conn->attached()) return true;
    QSignalSpy spy(conn, &NvimConnector::attachedChanged);
    return spy.wait(timeoutMs) && conn->attached();
}

} // namespace qvim::test
