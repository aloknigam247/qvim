#include <QSignalSpy>
#include <QtTest>

#include "Config.h"
#include "ConfigGGlobalReader.h"

using namespace qvim;

class TestConfigGReader : public QObject {
    Q_OBJECT
private slots:
    void appliesRegisteredVarsAndSkipsUnknown() {
        Config cfg;
        cfg.registerOption(QStringLiteral("opacity"), ConfigType::Float, 1.0);
        cfg.registerOption(QStringLiteral("padding"), ConfigType::Int, 0);

        QHash<QString, QVariant> vars;
        vars.insert(QStringLiteral("qvim_opacity"), 0.7);
        vars.insert(QStringLiteral("qvim_padding"), 8);
        vars.insert(QStringLiteral("qvim_unknown_var"), 42);

        ConfigGGlobalReader::readFromMap(vars, cfg);

        QCOMPARE(cfg.value(QStringLiteral("opacity")).toDouble(), 0.7);
        QCOMPARE(cfg.value(QStringLiteral("padding")).toInt(), 8);
        QVERIFY(!cfg.has(QStringLiteral("unknown_var")));
    }

    void typeCoercionForStringList() {
        Config cfg;
        cfg.registerOption(QStringLiteral("fallback"), ConfigType::StringList, QStringList{});
        QHash<QString, QVariant> vars;
        vars.insert(QStringLiteral("qvim_fallback"), QVariant(QStringList{ "a", "b", "c" }));
        ConfigGGlobalReader::readFromMap(vars, cfg);
        const QStringList expected{ "a", "b", "c" };
        QCOMPARE(cfg.value(QStringLiteral("fallback")).toStringList(), expected);
    }

    void missingVarsAreSilent() {
        Config cfg;
        cfg.registerOption(QStringLiteral("opacity"), ConfigType::Float, 1.0);
        QHash<QString, QVariant> vars;
        QSignalSpy spy(&cfg, &Config::changed);
        ConfigGGlobalReader::readFromMap(vars, cfg);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(cfg.value(QStringLiteral("opacity")).toDouble(), 1.0);
    }

    void boolCoercedFromInteger() {
        Config cfg;
        cfg.registerOption(QStringLiteral("frameless"), ConfigType::Bool, false);
        QHash<QString, QVariant> vars;
        vars.insert(QStringLiteral("qvim_frameless"), 1);
        ConfigGGlobalReader::readFromMap(vars, cfg);
        QCOMPARE(cfg.value(QStringLiteral("frameless")).toBool(), true);
    }
};

QTEST_GUILESS_MAIN(TestConfigGReader)
#include "test_config_g_reader.moc"
