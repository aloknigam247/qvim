#include "SessionEventBuffer.h"

#include <QJsonDocument>

namespace qvim {

SessionEventBuffer::SessionEventBuffer(int maxEntries) : m_maxEntries(qMax(1, maxEntries)) {}

BufferedEvent SessionEventBuffer::append(QJsonObject frame) {
    const quint64 seq = m_nextSeq++;
    frame.insert(QStringLiteral("seq"), static_cast<qint64>(seq));

    BufferedEvent event{ .seq = seq, .json = QJsonDocument(frame).toJson(QJsonDocument::Compact) };
    m_events.push_back(event);
    while(static_cast<int>(m_events.size()) > m_maxEntries) { m_events.pop_front(); }
    return event;
}

} // namespace qvim
