#include <QtTest>
#include <QSignalSpy>

#include "Config.h"
#include "ConfigGGlobalReader.h"
#include "IntegrationHelpers.h"

using namespace qvim;
using namespace qvim::test;

class TestConfigGGlobals : public QObject {
    Q_OBJECT
private slots:
    void readsGGlobalSetViaForwardedCmd() {
        Config cfg;
        cfg.registerOption(QStringLiteral("test_value"), ConfigType::Float, 0.0);

        NvimConnector conn;
        const QStringList extra{
            QStringLiteral("--cmd"),
            QStringLiteral("let g:qvim_test_value = 0.42"),
        };
        QVERIFY(conn.start(locateNvim(), extra));
        QSignalSpy completeSpy(&conn, &NvimConnector::attachComplete);
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(completeSpy.wait(5000));

        QSignalSpy changedSpy(&cfg, &Config::changed);
        ConfigGGlobalReader::read(conn, cfg);
        QVERIFY(changedSpy.wait(5000));

        QCOMPARE(cfg.value(QStringLiteral("test_value")).toDouble(), 0.42);
    }
};

QTEST_GUILESS_MAIN(TestConfigGGlobals)
#include "test_config_g_globals.moc"
