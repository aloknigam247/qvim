#pragma once

#include <QtGlobal>

namespace qvim {

// Pure state machine driving the cursor blink. Time is passed in as monotonic
// milliseconds; the class itself owns no clock so it can be exercised under
// unit test with virtual timestamps. Matches the nvim mode_info_set contract:
//
//   blinkWait — quiet period after the last activity during which the cursor
//               stays solid on (no blink). In ms.
//   blinkOn   — duration of the "visible" half of each blink cycle, in ms.
//   blinkOff  — duration of the "hidden" half of each blink cycle, in ms.
//
// If any of the three is 0 (or negative) the cursor never blinks: isOn()
// returns true unconditionally and nextChangeMs() returns kNoChange.
class CursorBlinkState {
public:
    static constexpr qint64 kNoChange = -1;

    void setBlinkParams(int blinkWait, int blinkOn, int blinkOff);

    // Record activity (cursor move, mode change, focus gain). Forces the
    // cursor on and restarts the quiet period from `nowMs`.
    void notifyActivity(qint64 nowMs);

    bool isOn(qint64 nowMs) const;

    // Absolute timestamp of the next on/off transition, or kNoChange if no
    // further change is due. Callers use this to schedule a single-shot timer.
    qint64 nextChangeMs(qint64 nowMs) const;

private:
    bool blinkingEnabled() const { return m_blinkWait > 0 && m_blinkOn > 0 && m_blinkOff > 0; }

    int m_blinkWait = 0;
    int m_blinkOn = 0;
    int m_blinkOff = 0;
    qint64 m_activityAt = 0;
};

} // namespace qvim
