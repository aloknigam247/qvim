#include <QtTest>

#include "Config.h"
#include "ConfigCliReader.h"

using namespace qvim;

class TestConfigCliReader : public QObject {
    Q_OBJECT
private slots:
    void floatParsedAndArgConsumed() {
        Config cfg;
        cfg.registerOption(QStringLiteral("opacity"), ConfigType::Float, 1.0);
        QStringList args{ QStringLiteral("--qvim-opacity=0.5") };
        ConfigCliReader::extract(args, cfg);
        QCOMPARE(cfg.value(QStringLiteral("opacity")).toDouble(), 0.5);
        QVERIFY(args.isEmpty());
    }

    void boolBareFlagDefaultsTrue() {
        Config cfg;
        cfg.registerOption(QStringLiteral("frameless"), ConfigType::Bool, false);
        QStringList args{ QStringLiteral("--qvim-frameless") };
        ConfigCliReader::extract(args, cfg);
        QCOMPARE(cfg.value(QStringLiteral("frameless")).toBool(), true);
        QVERIFY(args.isEmpty());
    }

    void boolExplicitValueForms() {
        Config cfg;
        cfg.registerOption(QStringLiteral("frameless"), ConfigType::Bool, true);

        QStringList args{ QStringLiteral("--qvim-frameless=false") };
        ConfigCliReader::extract(args, cfg);
        QCOMPARE(cfg.value(QStringLiteral("frameless")).toBool(), false);

        QStringList args2{ QStringLiteral("--qvim-frameless=on") };
        ConfigCliReader::extract(args2, cfg);
        QCOMPARE(cfg.value(QStringLiteral("frameless")).toBool(), true);

        QStringList args3{ QStringLiteral("--qvim-frameless=0") };
        ConfigCliReader::extract(args3, cfg);
        QCOMPARE(cfg.value(QStringLiteral("frameless")).toBool(), false);
    }

    void intMixedWithForwardedArg() {
        Config cfg;
        cfg.registerOption(QStringLiteral("padding"), ConfigType::Int, 0);
        QStringList args{ QStringLiteral("--qvim-padding=10"), QStringLiteral("foo.txt") };
        ConfigCliReader::extract(args, cfg);
        QCOMPARE(cfg.value(QStringLiteral("padding")).toInt(), 10);
        QCOMPARE(args, QStringList{ QStringLiteral("foo.txt") });
    }

    void stringValue() {
        Config cfg;
        cfg.registerOption(QStringLiteral("theme"), ConfigType::String, QStringLiteral("light"));
        QStringList args{ QStringLiteral("--qvim-theme=dark") };
        ConfigCliReader::extract(args, cfg);
        QCOMPARE(cfg.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
    }

    void stringListSplitsOnComma() {
        Config cfg;
        cfg.registerOption(QStringLiteral("fallback"), ConfigType::StringList, QStringList{});
        QStringList args{ QStringLiteral("--qvim-fallback=a,b,c") };
        ConfigCliReader::extract(args, cfg);
        QCOMPARE(cfg.value(QStringLiteral("fallback")).toStringList(),
                 QStringList{ "a", "b", "c" });
    }

    void unknownOptionIgnored() {
        Config cfg;
        cfg.registerOption(QStringLiteral("padding"), ConfigType::Int, 0);
        QStringList args{ QStringLiteral("--qvim-unknown=x") };
        ConfigCliReader::extract(args, cfg);
        QCOMPARE(cfg.value(QStringLiteral("padding")).toInt(), 0);
    }

    void malformedValueLeavesDefault() {
        Config cfg;
        cfg.registerOption(QStringLiteral("padding"), ConfigType::Int, 7);
        QStringList args{ QStringLiteral("--qvim-padding=notanumber") };
        ConfigCliReader::extract(args, cfg);
        QCOMPARE(cfg.value(QStringLiteral("padding")).toInt(), 7);
    }
};

QTEST_GUILESS_MAIN(TestConfigCliReader)
#include "test_config_cli_reader.moc"
