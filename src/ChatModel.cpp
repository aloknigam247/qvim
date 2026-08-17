#include "ChatModel.h"

#include <QTimer>

namespace qvim {

namespace {
// Interval between streamed chunks. Small enough that the echo assembles
// promptly, large enough that each chunk is a distinct model update the
// streaming-assembly path (and its tests) actually observes.
constexpr int kStreamIntervalMs = 16;
constexpr int kEchoChunks       = 3;
} // namespace

ChatModel::ChatModel(QObject* parent)
    : QAbstractListModel(parent)
    , m_streamTimer(new QTimer(this)) {
    m_streamTimer->setInterval(kStreamIntervalMs);
    connect(m_streamTimer, &QTimer::timeout, this, &ChatModel::streamTick);
}

int ChatModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_msgs.size());
}

QVariant ChatModel::data(const QModelIndex& index, int role) const {
    const int row = index.row();
    if (row < 0 || row >= m_msgs.size()) return {};
    switch (role) {
        case AuthorRole: return authorName(m_msgs[row].author);
        case TextRole:   return m_msgs[row].text;
        default:         return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const {
    return {
        {AuthorRole, QByteArrayLiteral("author")},
        {TextRole,   QByteArrayLiteral("text")},
    };
}

void ChatModel::submit(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;

    ++m_turn;
    const QString userId      = QStringLiteral("u") + QString::number(m_turn);
    const QString assistantId = QStringLiteral("a") + QString::number(m_turn);

    appendMessage(Author::User, trimmed);
    emit userMessageAdded(userId, trimmed);

    // Begin an empty assistant block, then queue the echo split into chunks
    // targeting that specific row.
    appendMessage(Author::Assistant, QString());
    const int assistantRow = static_cast<int>(m_msgs.size()) - 1;
    emit assistantMessageBegan(assistantId);

    const QString reply = QStringLiteral("Echo: ") + trimmed;
    const QVector<QString> parts = chunkify(reply, kEchoChunks);
    for (int i = 0; i < parts.size(); ++i) {
        const bool last = (i == parts.size() - 1);
        m_pending.enqueue(Chunk{assistantRow, assistantId, parts[i], last});
    }
    if (!m_pending.isEmpty() && !m_streamTimer->isActive()) {
        m_streamTimer->start();
    }
}

void ChatModel::streamTick() {
    if (m_pending.isEmpty()) {
        m_streamTimer->stop();
        return;
    }
    const Chunk chunk = m_pending.dequeue();
    if (chunk.row >= 0 && chunk.row < m_msgs.size()) {
        m_msgs[chunk.row].text += chunk.text;
        const QModelIndex idx = index(chunk.row, 0);
        emit dataChanged(idx, idx, {TextRole});
    }
    emit assistantMessageDelta(chunk.id, chunk.text);
    if (chunk.last) {
        emit assistantMessageEnded(chunk.id);
    }
    if (m_pending.isEmpty()) {
        m_streamTimer->stop();
    }
}

QString ChatModel::textAt(int row) const {
    if (row < 0 || row >= m_msgs.size()) return {};
    return m_msgs[row].text;
}

QString ChatModel::authorAt(int row) const {
    if (row < 0 || row >= m_msgs.size()) return {};
    return authorName(m_msgs[row].author);
}

void ChatModel::appendMessage(Author author, const QString& text) {
    const int row = static_cast<int>(m_msgs.size());
    beginInsertRows({}, row, row);
    m_msgs.push_back(Message{author, text});
    endInsertRows();
    emit countChanged();
}

QString ChatModel::authorName(Author a) {
    return a == Author::User ? QStringLiteral("user")
                             : QStringLiteral("assistant");
}

QVector<QString> ChatModel::chunkify(const QString& s, int parts) {
    QVector<QString> out;
    if (s.isEmpty()) return out;
    const int n     = qMax(1, parts);
    const int len   = static_cast<int>(s.size());
    const int chunk = (len + n - 1) / n; // ceil division
    for (int i = 0; i < len; i += chunk) {
        out.push_back(s.mid(i, chunk));
    }
    return out;
}

} // namespace qvim
