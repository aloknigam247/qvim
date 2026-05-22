#include <QSignalSpy>
#include <QtTest>

#include "ResizeCoalescer.h"

using namespace qvim;

class TestResizeCoalescer : public QObject {
    Q_OBJECT
private slots:
    void burstCoalescesToFinalValue() {
        ResizeCoalescer c;
        c.setIntervalMs(5);
        QSignalSpy spy(&c, &ResizeCoalescer::resizeRequested);

        for (int i = 0; i < 20; ++i) {
            c.requestResize(100 + i, 30 + i);
        }
        QVERIFY(spy.wait(200));
        // No further fires after the single debounce window.
        QTest::qWait(50);
        QCOMPARE(spy.count(), 1);
        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 119);
        QCOMPARE(args.at(1).toInt(), 49);
    }

    void duplicateValuesSuppressedOnSecondCycle() {
        ResizeCoalescer c;
        c.setIntervalMs(5);
        QSignalSpy spy(&c, &ResizeCoalescer::resizeRequested);

        c.requestResize(80, 24);
        QVERIFY(spy.wait(200));
        QCOMPARE(spy.count(), 1);

        // Same target values — should NOT emit again.
        c.requestResize(80, 24);
        QTest::qWait(50);
        QCOMPARE(spy.count(), 1);
    }

    void differentValuesAcrossCyclesProduceTwoRpcs() {
        ResizeCoalescer c;
        c.setIntervalMs(5);
        QSignalSpy spy(&c, &ResizeCoalescer::resizeRequested);

        c.requestResize(80, 24);
        QVERIFY(spy.wait(200));
        c.requestResize(120, 40);
        QVERIFY(spy.wait(200));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toInt(), 120);
        QCOMPARE(spy.at(1).at(1).toInt(), 40);
    }

    void flushNowFiresImmediately() {
        ResizeCoalescer c;
        c.setIntervalMs(10000);  // long, so we know flushNow bypassed it
        QSignalSpy spy(&c, &ResizeCoalescer::resizeRequested);

        c.requestResize(90, 28);
        c.flushNow();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 90);
        QCOMPARE(spy.at(0).at(1).toInt(), 28);
    }
};

QTEST_GUILESS_MAIN(TestResizeCoalescer)
#include "test_resize_coalescer.moc"
