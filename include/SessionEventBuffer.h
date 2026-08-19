#pragma once

#include <deque>
#include <QByteArray>
#include <QJsonObject>

namespace qvim {

// One buffered server→client frame: its assigned sequence number plus the exact
// compact-JSON bytes broadcast on the wire.
struct BufferedEvent {
    quint64 seq;
    QByteArray json;
};

// Bounded, in-memory ring of the most recent server→client frames, keyed by a
// monotonic, never-reused `seq`. This is not disk logging — nothing is written
// to a file; the buffer lives only in RAM for the lifetime of the server.
//
// It serves two purposes: it stamps the next `seq` onto each outgoing frame (so
// every subscriber shares one ordering), and it retains recent frames so a
// reconnecting client can replay from the `seq` it last applied. `append()`
// stamps the next `seq`, serialises the frame to compact JSON, appends it, and
// evicts the oldest entry while over the bound — the counter only ever advances,
// so an evicted `seq` is never handed out again.
class SessionEventBuffer {
public:
    explicit SessionEventBuffer(int maxEntries = 1024);

    // Stamps `frame["seq"]` with the next sequence number, serialises it to
    // compact JSON, appends it, and evicts the oldest entry while over the
    // bound. Returns the stored event (seq + wire bytes).
    BufferedEvent append(QJsonObject frame);

    // The `seq` the next append() will assign. Never decreases.
    quint64 nextSeq() const { return m_nextSeq; }

    int size() const { return static_cast<int>(m_events.size()); }

    const std::deque<BufferedEvent> &entries() const { return m_events; }

private:
    quint64 m_nextSeq = 1;
    int m_maxEntries;
    std::deque<BufferedEvent> m_events;
};

} // namespace qvim
