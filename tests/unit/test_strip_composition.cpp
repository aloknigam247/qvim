// Headless unit test for stripRowPlacements — the pure geometry that lays out
// the scroll-transition strip (outgoing snapshot rows + surviving model rows).
// The animatable scroll gate only ever fires for top==0 full-width regions, so
// the strip is exercised here with top==0 and both scroll directions. The core
// invariant: once the eased offset is applied, the clip band [0, bot*ch) is
// covered by exactly one strip row per band row — no gap, no overlap — and the
// row identities reproduce a hand-computed scroll at both the seed offset (old
// image) and offset 0 (new image).

#include <QtTest>

#include "StripComposition.h"

using namespace qvim;

namespace {

// For a given applied offset, map each strip row that lands exactly on a band
// row of [0, bot) to that band row. Asserts single-source coverage: returns a
// bot-sized vector where entry j is the (fromLost,index) that fills band row j,
// and fails via the out-params if any band row is uncovered or double-covered.
struct Cover {
    bool fromLost;
    int index;
};

QVector<Cover> coverBand(const QVector<StripRow> &rows, int bot, qreal ch, qreal offset,
                         int &covered, int &collisions) {
    QVector<Cover> band(bot, Cover{ false, -1 });
    QVector<int> hits(bot, 0);
    for(const StripRow &r: rows) {
        const qreal effY = r.baseY + offset;
        const qreal q = effY / ch;
        const int j = qRound(q);
        // Only rows that land exactly on a band row (aligned endpoints) count.
        if(qAbs(q - j) > 1e-9) continue;
        if(j < 0 || j >= bot) continue;
        band[j] = Cover{ r.fromLost, r.index };
        ++hits[j];
    }
    covered = 0;
    collisions = 0;
    for(int j = 0; j < bot; ++j) {
        if(hits[j] >= 1) ++covered;
        if(hits[j] > 1) ++collisions;
    }
    return band;
}

} // namespace

class TestStripComposition : public QObject {
    Q_OBJECT
private slots:
    void emptyForZeroDeltaOrEmptyBand() {
        QVERIFY(stripRowPlacements(0, 0, 10, 16.0).isEmpty());
        QVERIFY(stripRowPlacements(1, 5, 5, 16.0).isEmpty());
        QVERIFY(stripRowPlacements(1, 6, 3, 16.0).isEmpty());
    }

    void contiguousTilingNoGapNoOverlap() {
        // The raw strip (before any offset) is a contiguous vertical tiling:
        // sorted baseY values step by exactly cellHeight with no gap or overlap,
        // and the row count is (bot-top) + |delta|.
        const qreal ch = 16.0;
        for(int delta: { 3, -3, 1, -1 }) {
            const auto rows = stripRowPlacements(delta, 0, 10, ch);
            QCOMPARE(rows.size(), 10 + qAbs(delta));
            QVector<qreal> ys;
            for(const auto &r: rows) ys.push_back(r.baseY);
            std::sort(ys.begin(), ys.end());
            for(int i = 1; i < ys.size(); ++i) {
                QVERIFY2(qAbs((ys[i] - ys[i - 1]) - ch) < 1e-9,
                         qPrintable(QStringLiteral("delta %1: gap/overlap between strip rows %2,%3")
                                        .arg(delta)
                                        .arg(ys[i - 1])
                                        .arg(ys[i])));
            }
        }
    }

    void scrollDownReproducesOldThenNew() {
        // delta > 0: content scrolled up; outgoing rows left the top. Seed
        // offset = +delta*ch (old image), settle offset = 0 (new image).
        const qreal ch = 16.0;
        const int bot = 10, n = 3;
        const auto rows = stripRowPlacements(n, 0, bot, ch);

        int covered = 0, collisions = 0;
        auto atSeed = coverBand(rows, bot, ch, n * ch, covered, collisions);
        QCOMPARE(covered, bot);
        QCOMPARE(collisions, 0);
        for(int j = 0; j < bot; ++j) {
            if(j < n) {
                QVERIFY(atSeed[j].fromLost);
                QCOMPARE(atSeed[j].index, j); // outgoing row j
            } else {
                QVERIFY(!atSeed[j].fromLost);
                QCOMPARE(atSeed[j].index, j - n); // surviving model row
            }
        }

        auto atRest = coverBand(rows, bot, ch, 0.0, covered, collisions);
        QCOMPARE(covered, bot);
        QCOMPARE(collisions, 0);
        for(int j = 0; j < bot; ++j) {
            QVERIFY(!atRest[j].fromLost);
            QCOMPARE(atRest[j].index, j); // final: every band row is model row j
        }
    }

    void scrollUpReproducesOldThenNew() {
        // delta < 0: content scrolled down; outgoing rows left the bottom. Seed
        // offset = delta*ch = -n*ch (old image), settle offset = 0 (new image).
        const qreal ch = 16.0;
        const int bot = 10, n = 3;
        const auto rows = stripRowPlacements(-n, 0, bot, ch);

        int covered = 0, collisions = 0;
        auto atSeed = coverBand(rows, bot, ch, -n * ch, covered, collisions);
        QCOMPARE(covered, bot);
        QCOMPARE(collisions, 0);
        for(int j = 0; j < bot; ++j) {
            if(j < bot - n) {
                QVERIFY(!atSeed[j].fromLost);
                QCOMPARE(atSeed[j].index, j + n); // surviving model row shifted down
            } else {
                QVERIFY(atSeed[j].fromLost);
                QCOMPARE(atSeed[j].index, j - (bot - n)); // outgoing row
            }
        }

        auto atRest = coverBand(rows, bot, ch, 0.0, covered, collisions);
        QCOMPARE(covered, bot);
        QCOMPARE(collisions, 0);
        for(int j = 0; j < bot; ++j) {
            QVERIFY(!atRest[j].fromLost);
            QCOMPARE(atRest[j].index, j);
        }
    }
};

QTEST_GUILESS_MAIN(TestStripComposition)
#include "test_strip_composition.moc"
