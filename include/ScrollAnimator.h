#ifndef SCROLLANIMATOR_H
#define SCROLLANIMATOR_H

#include <QEasingCurve>
#include <QtGlobal>

namespace qvim {

// Pure easing state for the grid smooth-scroll offset. Time is passed in as
// monotonic milliseconds; the class owns no clock so it can be exercised under
// unit test with virtual timestamps (mirrors CursorBlinkState).
//
// A scroll seeds a signed pixel offset that eases to zero over durationMs with
// an OutExpo curve (matching the cursor-move animation). offsetAt() is a pure
// function of the seeded offset, the start timestamp, and the query time.
//
// Coalescing is snap-then-restart: a new start() while an ease is in flight
// simply reseeds from the new step, so the snapshot backing the current
// animation is always the current step's outgoing rows — never a stale strip.
class ScrollAnimator {
public:
    static constexpr qint64 kDurationMs = 125;

    // Seed an ease of `pixelOffset` -> 0 starting at nowMs. pixelOffset is the
    // signed distance the content still has to travel (positive = the strip
    // slides up, i.e. content scrolled down).
    void start(qreal pixelOffset, qint64 nowMs) {
        m_startOffset = pixelOffset;
        m_startMs = nowMs;
    }

    // Finish immediately: offset jumps to zero (paste / large jump / any
    // incompatible batch).
    void snap() { m_startOffset = 0.0; }

    // Remaining eased offset at nowMs; exactly 0.0 once the duration elapses or
    // after snap().
    qreal offsetAt(qint64 nowMs) const {
        if(m_startOffset == 0.0) return 0.0;
        const qint64 elapsed = nowMs - m_startMs;
        if(elapsed <= 0) return m_startOffset;
        if(elapsed >= kDurationMs) return 0.0;
        const qreal t = static_cast<qreal>(elapsed) / static_cast<qreal>(kDurationMs);
        const qreal progress = m_curve.valueForProgress(t); // 0 -> 1
        return m_startOffset * (1.0 - progress);
    }

    bool active(qint64 nowMs) const {
        return m_startOffset != 0.0 && (nowMs - m_startMs) < kDurationMs;
    }

private:
    QEasingCurve m_curve{ QEasingCurve::OutExpo };
    qreal m_startOffset = 0.0;
    qint64 m_startMs = 0;
};

} // namespace qvim

#endif
