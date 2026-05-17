#include <QtQuickTest>
#include <QQmlEngine>
#include <QQmlContext>
#include <QObject>

#include "NvimConnector.h"

namespace {

class TestSetup : public QObject {
    Q_OBJECT
public:
    explicit TestSetup(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        // Provide a real NvimConnector but do NOT call start() — its child
        // models are valid QObjects with sensible defaults for binding tests.
        auto* connector = new qvim::NvimConnector(this);
        engine->rootContext()->setContextProperty(QStringLiteral("$connector"), connector);
    }
};

} // namespace

QUICK_TEST_MAIN_WITH_SETUP(qml_tests, TestSetup)

#include "main_qmltest.moc"
