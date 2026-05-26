#include <QByteArray>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <cstdio>
#ifdef _WIN32
#  include <fcntl.h>
#  include <io.h>
#endif

#include "AppIcon.h"
#include "ArgvParser.h"
#include "ClipboardBridge.h"
#include "Config.h"
#include "ConfigCliReader.h"
#include "ConfigGGlobalReader.h"
#include "HighlightTable.h"
#include "MsgpackRpc.h"
#include "NvimConnector.h"
#include "RecentProjectsModel.h"
#include "WindowChrome.h"

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

    // `qvim -`: slurp stdin synchronously before Qt is up. Reading via the CRT
    // works even though qvim is /SUBSYSTEM:WINDOWS — when the parent shell
    // redirects stdin (e.g. `echo test | qvim -`), the inherited handle is a
    // valid pipe. QProcess gives the embedded nvim its own RPC stdin pipe, so
    // qvim's stdin stays ours to consume.
    QByteArray stdinPayload;
    if (cli.stdinAsBuffer) {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        char buf[4096];
        while (std::size_t n = std::fread(buf, 1, sizeof(buf), stdin)) {
            stdinPayload.append(buf, static_cast<qsizetype>(n));
        }
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qvim"));
    app.setOrganizationName(QStringLiteral("qvim"));
    qvim::setupApplicationIcon(app);
    qRegisterMetaType<qvim::Notification>("qvim::Notification");
    qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");

    qvim::Config cfg;
    cfg.registerOption(QStringLiteral("rounded_highlights"),
                       qvim::ConfigType::StringList,
                       QStringList{});

    QStringList forwardArgs = cli.nvimForwardArgs;
    qvim::ConfigCliReader::extract(forwardArgs, cfg);

    qvim::NvimConnector connector;
    qvim::ClipboardBridge clipboard;
    qvim::RecentProjectsModel recents;
    qvim::WindowChrome windowChrome;

    if (!connector.start(locateNvim(), forwardArgs)) {
        qFatal("Failed to start nvim. Ensure it is on PATH.");
        return 1;
    }
    clipboard.attachTo(&connector);

    // Push the resolved rounded_highlights list into HighlightTable on
    // startup and whenever Config changes — must be wired BEFORE the
    // attachComplete handler runs ConfigGGlobalReader::read, because that
    // will fire Config::changed once the user's g:qvim_rounded_highlights
    // resolves.
    auto applyRounded = [&]() {
        if (auto* h = connector.highlights()) {
            h->setRoundedHighlights(
                cfg.value(QStringLiteral("rounded_highlights")).toStringList());
        }
    };
    applyRounded();
    QObject::connect(&cfg, &qvim::Config::changed, &connector,
                     [applyRounded](const QString& name) {
                         if (name == QStringLiteral("rounded_highlights")) applyRounded();
                     });

    QObject::connect(&connector, &qvim::NvimConnector::disconnected,
                     &app, &QGuiApplication::quit);
    QObject::connect(&connector, &qvim::NvimConnector::attachComplete,
                     &cfg, [&connector, &cfg]() {
                         qvim::ConfigGGlobalReader::read(connector, cfg);
                     });

    if (cli.stdinAsBuffer) {
        QObject::connect(&connector, &qvim::NvimConnector::attachComplete,
                         &connector, [&connector, stdinPayload]() {
                             connector.loadStdinIntoBuffer(stdinPayload);
                         });
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("$config"),    &cfg);
    engine.rootContext()->setContextProperty(QStringLiteral("$connector"), &connector);
    engine.rootContext()->setContextProperty(QStringLiteral("$clipboard"), &clipboard);
    engine.rootContext()->setContextProperty(QStringLiteral("$recents"),   &recents);
    engine.rootContext()->setContextProperty(QStringLiteral("$windowChrome"), &windowChrome);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []{ QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
    return app.exec();
}
