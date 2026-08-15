#pragma once

#include <QAbstractListModel>
#include <QQueue>
#include <QString>
#include <QVector>
#include <qqmlregistration.h>

class QTimer;

namespace qvim {

// Front-end chat model: a list of typed message blocks (user / assistant) plus
// a stub "echo" responder. There is deliberately NO backend and NO provider
// abstraction here (see issue #28) — submit() appends the user block then
// streams "Echo: <text>" into a new assistant block in a few chunks, so the
// streaming-assembly path exists from day one.
//
// Creatable QML_ELEMENT (unlike the nvim-owned models, which are
// QML_UNCREATABLE): chat is qvim-native, drives no nvim RPC, and is
// instantiated directly by ChatPanel.qml — which keeps it unit-testable in
// isolation without a live NvimConnector.
class ChatModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        AuthorRole = Qt::UserRole + 1,
        TextRole,
    };

    explicit ChatModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_msgs.size()); }

    // Appends the user block for `text`, then queues "Echo: <text>" as a new
    // assistant block streamed in a few chunks. No-op on empty/whitespace.
    Q_INVOKABLE void submit(const QString& text);

    // Read helpers for bindings and tests. Return empty on out-of-range.
    Q_INVOKABLE QString textAt(int row) const;
    Q_INVOKABLE QString authorAt(int row) const;

signals:
    void countChanged();

private:
    enum class Author { User, Assistant };

    struct Message {
        Author  author;
        QString text;
    };

    // A queued streaming fragment carries its target row so a second submit()
    // arriving mid-stream can never append chunks to the wrong assistant block.
    struct Chunk {
        int     row;
        QString text;
    };

    void appendMessage(Author author, const QString& text);
    void streamTick();

    static QString              authorName(Author a);
    static QVector<QString>     chunkify(const QString& s, int parts);

    QVector<Message> m_msgs;
    QQueue<Chunk>    m_pending;
    QTimer*          m_streamTimer = nullptr;
};

} // namespace qvim
