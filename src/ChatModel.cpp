#include "ChatModel.h"

namespace qvim {

ChatModel::ChatModel(QObject *parent) : QAbstractListModel(parent) {}

int ChatModel::rowCount(const QModelIndex &parent) const {
    if(parent.isValid()) return 0;
    return static_cast<int>(m_msgs.size());
}

QVariant ChatModel::data(const QModelIndex &index, int role) const {
    const int row = index.row();
    if(row < 0 || row >= m_msgs.size()) return {};
    switch(role) {
        case AuthorRole:
            return authorName(m_msgs[row].author);
        case TextRole:
            return m_msgs[row].text;
        default:
            return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const {
    return {
        { AuthorRole, QByteArrayLiteral("author") },
        { TextRole, QByteArrayLiteral("text") },
    };
}

void ChatModel::appendBlock(const QString &author, const QString &text) {
    if(text.isEmpty()) return;
    const Author a = authorFromName(author);
    appendMessage(a, text);
    const QString id = QStringLiteral("x") + QString::number(++m_block);
    emit messageAdded(id, authorName(a), text);
}

QString ChatModel::textAt(int row) const {
    if(row < 0 || row >= m_msgs.size()) return {};
    return m_msgs[row].text;
}

QString ChatModel::authorAt(int row) const {
    if(row < 0 || row >= m_msgs.size()) return {};
    return authorName(m_msgs[row].author);
}

void ChatModel::appendMessage(Author author, const QString &text) {
    const int row = static_cast<int>(m_msgs.size());
    beginInsertRows({}, row, row);
    m_msgs.push_back(Message{ author, text });
    endInsertRows();
    emit countChanged();
}

QString ChatModel::authorName(Author a) {
    switch(a) {
        case Author::User:
            return QStringLiteral("user");
        case Author::Assistant:
            return QStringLiteral("assistant");
        case Author::System:
            return QStringLiteral("system");
    }
    return QStringLiteral("system");
}

ChatModel::Author ChatModel::authorFromName(const QString &name) {
    if(name == QStringLiteral("user")) return Author::User;
    if(name == QStringLiteral("assistant")) return Author::Assistant;
    return Author::System;
}

} // namespace qvim
