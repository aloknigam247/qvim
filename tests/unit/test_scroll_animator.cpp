// Headless unit test for ScrollAnimator, the pure easing helper behind the
// grid smooth-scroll. Time is injected as virtual milliseconds so the eased
// offset is deterministic and hard-assertable — no event loop, no clock.

#include <cmath>
#include <QtTest>

#include "ScrollAnimator.h"

using namespace qvim;

class TestScrollAnimator : public QObject {
    Q_OBJECT
private slots:
    void offsetIsSeedAtStart() {
        ScrollAnimator a;
        a.start(16.0, 1000);
        QCOMPARE(a.offsetAt(1000), 16.0);
        QVERIFY(a.active(1000));
    }

    void offsetEasesButStaysNonzeroMidway() {
        ScrollAnimator a;
        a.start(16.0, 0);
        const qreal mid = a.offsetAt(ScrollAnimator::kDurationMs / 2);
        // OutExpo has decayed the remaining offset well below the seed but not
        // yet to zero.
        QVERIFY2(mid > 0.0 && mid < 16.0,
                 qPrintable(QStringLiteral("mid offset %1 not strictly inside (0,16)").arg(mid)));
        QVERIFY(a.active(ScrollAnimator::kDurationMs / 2));
    }

    void offsetIsExactlyZeroAtAndAfterDuration() {
        ScrollAnimator a;
        a.start(-16.0, 500);
        QCOMPARE(a.offsetAt(500 + ScrollAnimator::kDurationMs), 0.0);
        QCOMPARE(a.offsetAt(500 + ScrollAnimator::kDurationMs + 1000), 0.0);
        QVERIFY(!a.active(500 + ScrollAnimator::kDurationMs));
    }

    void negativeSeedEasesTowardZeroFromBelow() {
        ScrollAnimator a;
        a.start(-16.0, 0);
        const qreal mid = a.offsetAt(ScrollAnimator::kDurationMs / 2);
        QVERIFY2(mid < 0.0 && mid > -16.0,
                 qPrintable(QStringLiteral("mid offset %1 not strictly inside (-16,0)").arg(mid)));
    }

    void snapForcesOffsetToZero() {
        ScrollAnimator a;
        a.start(16.0, 0);
        a.snap();
        QCOMPARE(a.offsetAt(0), 0.0);
        QCOMPARE(a.offsetAt(ScrollAnimator::kDurationMs / 2), 0.0);
        QVERIFY(!a.active(0));
    }

    void startMidEaseReseedsToNewSingleStep() {
        // Snap-then-restart: a fresh start() while an ease is in flight discards
        // the old step entirely and eases the new seed from full magnitude.
        ScrollAnimator a;
        a.start(16.0, 0);
        // Partway through the first ease, a new scroll arrives.
        a.start(8.0, ScrollAnimator::kDurationMs / 2);
        QCOMPARE(a.offsetAt(ScrollAnimator::kDurationMs / 2), 8.0);
        QVERIFY(a.active(ScrollAnimator::kDurationMs / 2));
        // And it completes one full duration after the *new* start.
        QCOMPARE(a.offsetAt(ScrollAnimator::kDurationMs / 2 + ScrollAnimator::kDurationMs), 0.0);
    }

    void offsetBeforeStartClampsToSeed() {
        ScrollAnimator a;
        a.start(16.0, 100);
        // A query at or before the start timestamp yields the full seed.
        QCOMPARE(a.offsetAt(50), 16.0);
    }
};

QTEST_GUILESS_MAIN(TestScrollAnimator)
#include "test_scroll_animator.moc"
