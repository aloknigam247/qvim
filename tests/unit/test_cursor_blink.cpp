#include <QtTest>

#include "CursorBlinkState.h"

using namespace qvim;

class TestCursorBlink : public QObject {
    Q_OBJECT
private slots:
    void solidDuringQuietPeriod() {
        CursorBlinkState s;
        s.setBlinkParams(700, 400, 250);
        s.notifyActivity(1000);
        QVERIFY(s.isOn(1000));
        QVERIFY(s.isOn(1500));
        QVERIFY(s.isOn(1699));
    }

    void blinksAfterQuietPeriod() {
        CursorBlinkState s;
        s.setBlinkParams(700, 400, 250);
        s.notifyActivity(1000);
        // t0 + blinkWait = 1700; first post-quiet phase is "on" for blinkOn ms.
        QVERIFY(s.isOn(1700));
        QVERIFY(s.isOn(2099));
        QVERIFY(!s.isOn(2100));
        QVERIFY(!s.isOn(2349));
        QVERIFY(s.isOn(2350));
        QVERIFY(s.isOn(2749));
        QVERIFY(!s.isOn(2750));
    }

    void activityDuringOffPhaseFlipsOn() {
        CursorBlinkState s;
        s.setBlinkParams(500, 300, 200);
        s.notifyActivity(0);
        // After t=0 + 500 (wait) + 300 (on) = 800, off-phase starts.
        QVERIFY(!s.isOn(800));
        s.notifyActivity(800);
        // 800..1300 quiet (on), 1300..1600 on-phase, 1600..1800 off-phase
        QVERIFY(s.isOn(800));
        QVERIFY(s.isOn(1299));
        QVERIFY(s.isOn(1599));
        QVERIFY(!s.isOn(1600));
    }

    void zeroParamsDisableBlink() {
        CursorBlinkState s;
        s.setBlinkParams(0, 400, 250);
        s.notifyActivity(0);
        QVERIFY(s.isOn(0));
        QVERIFY(s.isOn(10000));
        QCOMPARE(s.nextChangeMs(0), CursorBlinkState::kNoChange);

        s.setBlinkParams(500, 0, 250);
        QVERIFY(s.isOn(1000));
        QCOMPARE(s.nextChangeMs(1000), CursorBlinkState::kNoChange);

        s.setBlinkParams(500, 300, 0);
        QVERIFY(s.isOn(1000));
        QCOMPARE(s.nextChangeMs(1000), CursorBlinkState::kNoChange);
    }

    void negativeParamsDisableBlink() {
        CursorBlinkState s;
        s.setBlinkParams(-1, 300, 200);
        s.notifyActivity(0);
        QVERIFY(s.isOn(1000));
        QCOMPARE(s.nextChangeMs(1000), CursorBlinkState::kNoChange);
    }

    void nextChangeMsDuringQuietPeriod() {
        CursorBlinkState s;
        s.setBlinkParams(700, 400, 250);
        s.notifyActivity(1000);
        // Quiet is solid-on; next on→off transition is t0+blinkWait+blinkOn.
        QCOMPARE(s.nextChangeMs(1000), qint64(2100));
        QCOMPARE(s.nextChangeMs(1699), qint64(2100));
    }

    void nextChangeMsDuringFirstOnPhase() {
        CursorBlinkState s;
        s.setBlinkParams(700, 400, 250);
        s.notifyActivity(1000);
        // First post-quiet on-phase 1700..2100; next change at 2100.
        QCOMPARE(s.nextChangeMs(1700), qint64(2100));
        QCOMPARE(s.nextChangeMs(2099), qint64(2100));
    }

    void nextChangeMsDuringOffPhase() {
        CursorBlinkState s;
        s.setBlinkParams(700, 400, 250);
        s.notifyActivity(1000);
        // Off-phase 2100..2350; next change at 2350.
        QCOMPARE(s.nextChangeMs(2100), qint64(2350));
        QCOMPARE(s.nextChangeMs(2349), qint64(2350));
    }

    void multipleCyclesAlternateCorrectly() {
        CursorBlinkState s;
        s.setBlinkParams(100, 200, 100);
        s.notifyActivity(0);
        // 0..100 quiet (on), 100..300 on, 300..400 off, 400..600 on, 600..700 off
        QVERIFY(s.isOn(50));
        QVERIFY(s.isOn(99));
        QVERIFY(s.isOn(100));
        QVERIFY(s.isOn(299));
        QVERIFY(!s.isOn(300));
        QVERIFY(!s.isOn(399));
        QVERIFY(s.isOn(400));
        QVERIFY(s.isOn(599));
        QVERIFY(!s.isOn(600));
    }

    void setBlinkParamsDoesNotResetTimer() {
        CursorBlinkState s;
        s.setBlinkParams(100, 200, 100);
        s.notifyActivity(0);
        // 0..100 quiet, 100..300 on, 300..400 off — t=350 is off.
        QVERIFY(!s.isOn(350));
        s.setBlinkParams(100, 200, 100);
        QVERIFY(!s.isOn(350));
    }
};

QTEST_GUILESS_MAIN(TestCursorBlink)
#include "test_cursor_blink.moc"
