#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <cstdio>

#include "ArgvParser.h"
#include "MsgpackRpc.h"
#include "NvimConnector.h"
#include "ClipboardBridge.h"
#include "RecentProjectsModel.h"

namespace {

QString locateNvim() {
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("nvim"));
    if (!fromPath.isEmpty()) return fromPath;
    return QStringLiteral("nvim");
}

} // namespace

int main(int argc, char* argv[]) {
    const qvim::QvimArgs cli = qvim::parseArgv(argc, argv);
    if (cli.helpRequested) {
        std::printf(
            "Usage: qvim [qvim-options] [nvim-options] [file ...]\n"
            "\n"
            "qvim is a Neovim GUI client. All arguments not in qvim's own\n"
            "namespace are forwarded to the embedded `nvim --embed` process.\n"
            "\n"
            "qvim options:\n"
            "  -h, --help       Show this help and exit.\n"
            "  -v, --version    Show qvim version and exit.\n"
            "\n"
            "Everything else (e.g. `foo.txt`, `-O a.txt b.txt`, `+10 foo.txt`,\n"
            "`-c \"set number\" foo.txt`) is forwarded to nvim.\n");
        return 0;
    }
    if (cli.versionRequested) {
        std::printf("qvim 0.1.0\n");
        return 0;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qvim"));
    app.setOrganizationName(QStringLiteral("qvim"));
    qRegisterMetaType<qvim::Notification>("qvim::Notification");
    qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");

    qvim::NvimConnector connector;
    qvim::ClipboardBridge clipboard;
    qvim::RecentProjectsModel recents;

    if (!connector.start(locateNvim(), cli.nvimForwardArgs)) {
        qFatal("Failed to start nvim. Ensure it is on PATH.");
        return 1;
    }
    clipboard.attachTo(&connector);

    QObject::connect(&connector, &qvim::NvimConnector::disconnected,
                     &app, &QGuiApplication::quit);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("$connector"), &connector);
    engine.rootContext()->setContextProperty(QStringLiteral("$clipboard"), &clipboard);
    engine.rootContext()->setContextProperty(QStringLiteral("$recents"),   &recents);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []{ QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
    return app.exec();
}
