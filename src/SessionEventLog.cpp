#include "SessionEventLog.h"

#include <QJsonDocument>

namespace qvim {

SessionEventLog::SessionEventLog(int maxEntries)
    : m_maxEntries(qMax(1, maxEntries)) {}

LoggedEvent SessionEventLog::append(QJsonObject frame) {
    const quint64 seq = m_nextSeq++;
    frame.insert(QStringLiteral("seq"), static_cast<qint64>(seq));

    LoggedEvent event{seq, QJsonDocument(frame).toJson(QJsonDocument::Compact)};
    m_events.push_back(event);
    while (static_cast<int>(m_events.size()) > m_maxEntries) {
        m_events.pop_front();
    }
    return event;
}

} // namespace qvim
