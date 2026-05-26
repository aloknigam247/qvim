#include "CursorBlinkState.h"

namespace qvim {

void CursorBlinkState::setBlinkParams(int blinkWait, int blinkOn, int blinkOff) {
    m_blinkWait = blinkWait < 0 ? 0 : blinkWait;
    m_blinkOn   = blinkOn   < 0 ? 0 : blinkOn;
    m_blinkOff  = blinkOff  < 0 ? 0 : blinkOff;
}

void CursorBlinkState::notifyActivity(qint64 nowMs) {
    m_activityAt = nowMs;
}

bool CursorBlinkState::isOn(qint64 nowMs) const {
    if (!blinkingEnabled()) return true;
    const qint64 elapsed = nowMs - m_activityAt;
    // Pre-activity timestamps and the entire quiet period read as solid on.
    if (elapsed < m_blinkWait) return true;
    const qint64 cycle = static_cast<qint64>(m_blinkOn) + m_blinkOff;
    const qint64 phase = (elapsed - m_blinkWait) % cycle;
    return phase < m_blinkOn;
}

qint64 CursorBlinkState::nextChangeMs(qint64 nowMs) const {
    if (!blinkingEnabled()) return kNoChange;
    const qint64 elapsed = nowMs - m_activityAt;
    // Still inside the quiet period: next change is the transition from
    // "solid on" to the first off-phase, i.e. t0 + blinkWait + blinkOn.
    if (elapsed < m_blinkWait) {
        return m_activityAt + static_cast<qint64>(m_blinkWait) + m_blinkOn;
    }
    const qint64 cycle = static_cast<qint64>(m_blinkOn) + m_blinkOff;
    const qint64 phase = (elapsed - m_blinkWait) % cycle;
    const qint64 cycleStart = nowMs - phase;
    if (phase < m_blinkOn) {
        return cycleStart + m_blinkOn;
    }
    return cycleStart + cycle;
}

} // namespace qvim
