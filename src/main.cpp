#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>

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
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qvim"));
    app.setOrganizationName(QStringLiteral("qvim"));
    qRegisterMetaType<qvim::Notification>("qvim::Notification");
    qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");

    qvim::NvimConnector connector;
    qvim::ClipboardBridge clipboard;
    qvim::RecentProjectsModel recents;

    if (!connector.start(locateNvim())) {
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
