#include "ArgvParser.h"

#include <QDebug>
#include <QString>
#include <QStringList>

namespace qvim {

// PowerShell 7 (with the default PSNativeCommandArgumentPassing=Legacy) mangles
// native-exe invocations when any arg contains `.\`, `/`, or other special
// chars. Single-arg case: `.\foo.txt` arrives as `".\foo.txt"`. Multi-arg case:
// `.\foo.txt -O .\bar.txt` arrives as ONE arg `".\foo.txt" "-O" ".\bar.txt"`.
// MSVC CRT and CommandLineToArgvW both parse this faithfully — both end up
// returning the merged single arg. The fix lives entirely on our side.
static void appendNormalized(const QString& raw, QStringList& out) {
    if (raw.size() >= 2 && raw.startsWith(QLatin1Char('"')) && raw.endsWith(QLatin1Char('"'))) {
        const QString inner = raw.mid(1, raw.size() - 2);
        if (inner.contains(QStringLiteral("\" \""))) {
            // pwsh crushed multiple args into one quoted token.
            for (const QString& piece : inner.split(QStringLiteral("\" \""))) {
                out << piece;
            }
            return;
        }
        out << inner;
        return;
    }
    out << raw;
}

QvimArgs parseArgv(int argc, char** argv) {
    QvimArgs out;
    QStringList logical;
    for (int i = 1; i < argc; ++i) {
        appendNormalized(QString::fromLocal8Bit(argv[i]), logical);
    }
    for (const QString& arg : logical) {
        if (arg == QStringLiteral("--help") || arg == QStringLiteral("-h")) {
            out.helpRequested = true;
            continue;
        }
        if (arg == QStringLiteral("--version") || arg == QStringLiteral("-v")) {
            out.versionRequested = true;
            continue;
        }
        if (arg.startsWith(QStringLiteral("--qvim-"))) {
            // Reserved namespace for future qvim-only options. Swallowed here
            // so it never leaks into nvim's argv, where it would be rejected.
            qDebug() << "qvim: ignoring reserved option" << arg;
            continue;
        }
        if (arg == QStringLiteral("-")) {
            // Consumed by qvim — forwarding to nvim --embed would conflict
            // with the RPC channel that already owns nvim's stdin.
            out.stdinAsBuffer = true;
            continue;
        }
        out.nvimForwardArgs << arg;
    }
    return out;
}

} // namespace qvim
