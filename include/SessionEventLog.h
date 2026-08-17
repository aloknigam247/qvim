#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <deque>

namespace qvim {

// One logged server→client frame: its assigned sequence number plus the exact
// compact-JSON bytes broadcast on the wire.
struct LoggedEvent {
    quint64    seq;
    QByteArray json;
};

// Bounded, append-only event log with a monotonic, never-reused `seq`. qvim owns
// the session and is the sole source of truth for `seq`; subscribers replay from
// it but never own state. `append()` stamps the next `seq` onto a frame,
// serialises it, and evicts the oldest entry once the bound is exceeded — the
// counter only ever advances, so an evicted `seq` is never handed out again.
class SessionEventLog {
public:
    explicit SessionEventLog(int maxEntries = 1024);

    // Stamps `frame["seq"]` with the next sequence number, serialises it to
    // compact JSON, appends it, and evicts the oldest entry while over the
    // bound. Returns the stored event (seq + wire bytes).
    LoggedEvent append(QJsonObject frame);

    // The `seq` the next append() will assign. Never decreases.
    quint64 nextSeq() const { return m_nextSeq; }

    int size() const { return static_cast<int>(m_events.size()); }

    const std::deque<LoggedEvent>& entries() const { return m_events; }

private:
    quint64                 m_nextSeq = 1;
    int                     m_maxEntries;
    std::deque<LoggedEvent> m_events;
};

} // namespace qvim
