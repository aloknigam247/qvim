#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QtTest>
#include <QVariant>

#include <optional>

#include "CmdlineModel.h"
#include "GridModel.h"
#include "NvimConnector.h"
#include "PopupMenuModel.h"
#include "TablineModel.h"

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
inline bool startTestNvim(NvimConnector &conn, const QStringList &extraArgs = {}) {
    QStringList args;
    args.reserve(extraArgs.size() + 1);
    args << QStringLiteral("--clean");
    args += extraArgs;
    return conn.start(locateNvim(), args);
}

inline bool waitForFlush(NvimConnector *conn, int timeoutMs = 5000) {
    QSignalSpy spy(conn, &NvimConnector::flush);
    return spy.wait(timeoutMs);
}

inline bool waitForAttach(NvimConnector *conn, int timeoutMs = 5000) {
    if(conn->attached()) return true;
    QSignalSpy spy(conn, &NvimConnector::attachedChanged);
    return spy.wait(timeoutMs) && conn->attached();
}

// Reads a global via nvim_get_var, spinning the event loop until the async
// callback fires. Returns nullopt on timeout or if the variable is unset.
inline std::optional<QVariant> getVarSync(NvimConnector &conn, const QString &name,
                                          int timeoutMs = 3000) {
    std::optional<QVariant> result;
    bool done = false;
    conn.getVar(name, [&](std::optional<QVariant> v) {
        result = std::move(v);
        done = true;
    });
    QElapsedTimer t;
    t.start();
    while(!done && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done ? result : std::nullopt;
}

// Evaluates a vimscript expression by round-tripping it through a scratch global.
// Avoids a dedicated nvim_eval path on NvimConnector for test-only needs.
inline std::optional<QVariant> evalSync(NvimConnector &conn, const QString &expr,
                                        int timeoutMs = 3000) {
    conn.command(QStringLiteral("let g:__qvim_eval = %1").arg(expr));
    return getVarSync(conn, QStringLiteral("__qvim_eval"), timeoutMs);
}

} // namespace qvim::test
