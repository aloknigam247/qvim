// Headless micro-benchmark for GridModel::scroll on a 200x60 grid. Profiles
// the cost of repeated single-row scrolls (the j/k/Ctrl-D/Ctrl-U hot path) so
// regressions in the model's cell-move routine surface in tier-1 CI rather
// than only manifesting as visible lag in the integration smoke test.
//
// Skippable with QVIM_SKIP_PERF=1 for sanitizer / debug-CI runs where wall
// timings are uninformative.

#include <QElapsedTimer>
#include <QtTest>
#include <msgpack.hpp>
#include <algorithm>
#include <vector>

#include "GridModel.h"

using namespace qvim;

namespace {

msgpack::object_handle packRepeatedCells(const std::string& text, int hl, int repeat) {
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_array(1);
    pk.pack_array(3);
    pk.pack(text);
    pk.pack(hl);
    pk.pack(repeat);
    msgpack::object_handle h;
    msgpack::unpack(h, buf.data(), buf.size());
    return h;
}

} // namespace

class TestScrollPerfMicro : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (qEnvironmentVariableIntValue("QVIM_SKIP_PERF") != 0) {
            QSKIP("QVIM_SKIP_PERF=1 — skipping perf micro-benchmark");
        }
    }

    void scrollUp200x60() {
        constexpr int kCols   = 200;
        constexpr int kRows   = 60;
        constexpr int kIters  = 100;

        GridModel g;
        g.resize(kCols, kRows);

        // Fill every row with a 200-wide run of 'x' at hl=1 so each scroll
        // actually moves non-blank text. Empty cells would still exercise the
        // copy path but at smaller QString-refcount cost.
        auto cells = packRepeatedCells("x", 1, kCols);
        for (int r = 0; r < kRows; ++r) {
            g.applyLine(r, 0, cells.get());
        }

        // Warm up so the first iteration's cache / branch-predictor cost is
        // not folded into the timed median.
        for (int i = 0; i < 5; ++i) {
            g.scroll(0, kRows, 0, kCols, 1);
        }

        std::vector<qint64> samples;
        samples.reserve(kIters);
        QElapsedTimer t;
        for (int i = 0; i < kIters; ++i) {
            t.start();
            g.scroll(0, kRows, 0, kCols, 1);
            samples.push_back(t.nsecsElapsed());
        }

        std::sort(samples.begin(), samples.end());
        const qint64 medianNs = samples[samples.size() / 2];
        const qint64 p95Ns    = samples[(samples.size() * 95) / 100];

        qDebug("GridModel::scroll 200x60 single-row: median=%lld us  p95=%lld us  iters=%d",
               medianNs / 1000, p95Ns / 1000, kIters);

        // Comfortable ceiling. On a release build the row-pointer-indirection
        // path lands well under 50us; on the worst-case row-by-row Cell-copy
        // path it was ~300us. 500us leaves headroom for slow CI hardware and
        // debug builds while still failing if someone accidentally restores
        // an O(rows*cols) per-cell copy.
        QVERIFY2(medianNs < 500'000,
                 qPrintable(QStringLiteral("median scroll latency %1us exceeds 500us ceiling")
                                .arg(medianNs / 1000)));
    }
};

QTEST_GUILESS_MAIN(TestScrollPerfMicro)
#include "test_scroll_perf_micro.moc"
