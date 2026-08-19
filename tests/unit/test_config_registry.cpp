#include <QSignalSpy>
#include <QtTest>

#include "Config.h"

using namespace qvim;

class TestConfigRegistry : public QObject {
    Q_OBJECT
private slots:
    void defaultsAndHas() {
        Config cfg;
        cfg.registerOption(QStringLiteral("opacity"), ConfigType::Float, 1.0);
        QVERIFY(cfg.has(QStringLiteral("opacity")));
        QCOMPARE(cfg.value(QStringLiteral("opacity")).toDouble(), 1.0);
    }

    void gGlobalUpdatesAndSignalDeduplicates() {
        Config cfg;
        cfg.registerOption(QStringLiteral("opacity"), ConfigType::Float, 1.0);

        QSignalSpy spy(&cfg, &Config::changed);
        cfg.setFromGGlobal(QStringLiteral("opacity"), 0.5);
        QCOMPARE(cfg.value(QStringLiteral("opacity")).toDouble(), 0.5);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("opacity"));

        // Same RESOLVED value again → no signal.
        cfg.setFromGGlobal(QStringLiteral("opacity"), 0.5);
        QCOMPARE(spy.count(), 0);
    }

    void cliOverridesGGlobal() {
        Config cfg;
        cfg.registerOption(QStringLiteral("opacity"), ConfigType::Float, 1.0);
        cfg.setFromGGlobal(QStringLiteral("opacity"), 0.5);
        QCOMPARE(cfg.value(QStringLiteral("opacity")).toDouble(), 0.5);

        cfg.setFromCli(QStringLiteral("opacity"), 0.25);
        QCOMPARE(cfg.value(QStringLiteral("opacity")).toDouble(), 0.25);

        // Setting g: again while CLI shadows → resolved value unchanged → no signal.
        QSignalSpy spy(&cfg, &Config::changed);
        cfg.setFromGGlobal(QStringLiteral("opacity"), 0.9);
        QCOMPARE(cfg.value(QStringLiteral("opacity")).toDouble(), 0.25);
        QCOMPARE(spy.count(), 0);
    }

    void unknownNameQueries() {
        Config cfg;
        QVERIFY(!cfg.has(QStringLiteral("unknown")));
        QVERIFY(!cfg.value(QStringLiteral("unknown")).isValid());
    }

    void unknownNameSettersAreNoOp() {
        Config cfg;
        QSignalSpy spy(&cfg, &Config::changed);
        cfg.setFromCli(QStringLiteral("unknown"), 1);
        cfg.setFromGGlobal(QStringLiteral("unknown"), 1);
        QCOMPARE(spy.count(), 0);
    }

    void registeredNamesAlphabetical() {
        Config cfg;
        cfg.registerOption(QStringLiteral("zeta"), ConfigType::Bool, false);
        cfg.registerOption(QStringLiteral("alpha"), ConfigType::Int, 0);
        cfg.registerOption(QStringLiteral("mu"), ConfigType::String, QString());
        const QStringList n = cfg.registeredNames();
        QCOMPARE(n, QStringList{ "alpha", "mu", "zeta" });
    }
};

QTEST_GUILESS_MAIN(TestConfigRegistry)
#include "test_config_registry.moc"
