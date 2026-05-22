#include "ArgvParser.h"

#include <QDebug>
#include <QString>

namespace qvim {

QvimArgs parseArgv(int argc, char** argv) {
    QvimArgs out;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
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
        out.nvimForwardArgs << arg;
    }
    return out;
}

} // namespace qvim
