// Tier-1 unit test for NvimConnector::onOptionSet(name, value). Exercises the
// option_set redraw handler without spinning up nvim: we drive the handler
// directly and assert that (a) the Q_PROPERTY updates, (b) the NOTIFY signal
// fires exactly once on a real change, (c) no signal fires on a redundant
// re-set (same value), and (d) unknown options + malformed payloads don't
// crash and don't emit any signal.
//
// Uses QTEST_MAIN (not GUILESS) because NvimConnector instantiates Q_PROPERTY
// targets that pull in QGuiApplication via the QML registration chain.

#include <QSignalSpy>
#include <QtTest>

#include "NvimConnector.h"

using namespace qvim;

class TestOptionSetDispatch : public QObject {
    Q_OBJECT
private slots:
    void guifontUpdatesPropertyAndFires() {
        NvimConnector c;
        QSignalSpy spy(&c, &NvimConnector::guifontChanged);
        c.onOptionSet(QStringLiteral("guifont"), QStringLiteral("Cascadia Mono:h14"));
        QCOMPARE(c.guifont(), QStringLiteral("Cascadia Mono:h14"));
        QCOMPARE(spy.count(), 1);
    }

    void guifontSameValueDoesNotFire() {
        NvimConnector c;
        c.onOptionSet(QStringLiteral("guifont"), QStringLiteral("Cascadia Mono:h14"));
        QSignalSpy spy(&c, &NvimConnector::guifontChanged);
        c.onOptionSet(QStringLiteral("guifont"), QStringLiteral("Cascadia Mono:h14"));
        QCOMPARE(c.guifont(), QStringLiteral("Cascadia Mono:h14"));
        QCOMPARE(spy.count(), 0);
    }

    void linespaceUpdatesPropertyAndFires() {
        NvimConnector c;
        QSignalSpy spy(&c, &NvimConnector::linespaceChanged);
        c.onOptionSet(QStringLiteral("linespace"), 4);
        QCOMPARE(c.linespace(), 4);
        QCOMPARE(spy.count(), 1);
    }

    void linespaceSameValueDoesNotFire() {
        NvimConnector c;
        c.onOptionSet(QStringLiteral("linespace"), 4);
        QSignalSpy spy(&c, &NvimConnector::linespaceChanged);
        c.onOptionSet(QStringLiteral("linespace"), 4);
        QCOMPARE(spy.count(), 0);
    }

    void unknownOptionIsHarmless() {
        NvimConnector c;
        // No specific signal to spy on — we just verify the call doesn't
        // crash and doesn't perturb any tracked field.
        const QString oldGuifont = c.guifont();
        const int oldLinespace = c.linespace();
        c.onOptionSet(QStringLiteral("unknown_future_option"), 42);
        QCOMPARE(c.guifont(), oldGuifont);
        QCOMPARE(c.linespace(), oldLinespace);
    }

    void emptyGuifontDoesNotCrash() {
        NvimConnector c;
        QSignalSpy spy(&c, &NvimConnector::guifontChanged);
        // m_guifont starts as "" — setting it to "" should be a no-op too.
        c.onOptionSet(QStringLiteral("guifont"), QString());
        QCOMPARE(c.guifont(), QString());
        QCOMPARE(spy.count(), 0);
    }

    void termguicolorsBool() {
        NvimConnector c;
        QSignalSpy spy(&c, &NvimConnector::termguicolorsChanged);
        c.onOptionSet(QStringLiteral("termguicolors"), true);
        QCOMPARE(c.termguicolors(), true);
        QCOMPARE(spy.count(), 1);
        c.onOptionSet(QStringLiteral("termguicolors"), true);
        QCOMPARE(spy.count(), 1);
    }

    void ambiwidthString() {
        NvimConnector c;
        QSignalSpy spy(&c, &NvimConnector::ambiwidthChanged);
        QCOMPARE(c.ambiwidth(), QStringLiteral("single"));
        c.onOptionSet(QStringLiteral("ambiwidth"), QStringLiteral("double"));
        QCOMPARE(c.ambiwidth(), QStringLiteral("double"));
        QCOMPARE(spy.count(), 1);
    }

    void extFlagSilentlyIgnored() {
        NvimConnector c;
        c.onOptionSet(QStringLiteral("ext_messages"), true);
        c.onOptionSet(QStringLiteral("ext_cmdline"), false);
        // No signals exposed for ext_* — just verify no crash.
    }

    void pumblendInt() {
        NvimConnector c;
        QSignalSpy spy(&c, &NvimConnector::pumblendChanged);
        c.onOptionSet(QStringLiteral("pumblend"), 30);
        QCOMPARE(c.pumblend(), 30);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestOptionSetDispatch)
#include "test_option_set_dispatch.moc"
