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

// Starts nvim with --clean (no user config, no plugins, no shada) plus any
// caller-supplied args. Using --clean isolates tests from the developer's
// init.vim / init.lua, so assertions like "row 0 starts with 'abc'" don't
// break on machines whose config adds `set number`, `set cmdheight=0`, etc.
// Tests that need specific nvim options should pass them explicitly via
// `--cmd 'set ...'` or `-u <path-to-test-config.lua>` in extraArgs.
inline bool startTestNvim(NvimConnector& conn, const QStringList& extraArgs = {}) {
    QStringList args;
    args.reserve(extraArgs.size() + 1);
    args << QStringLiteral("--clean");
    args += extraArgs;
    return conn.start(locateNvim(), args);
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
