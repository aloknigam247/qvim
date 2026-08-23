#pragma once

#include <QAbstractListModel>
#include <qqmlregistration.h>
#include <QQueue>
#include <QString>
#include <QVector>

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
        TextRole
    };

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_msgs.size()); }

    // Appends the user block for `text`, then queues "Echo: <text>" as a new
    // assistant block streamed in a few chunks. No-op on empty/whitespace.
    Q_INVOKABLE void submit(const QString &text);

    // Appends an externally-sourced block verbatim (no echo, no streaming). Used
    // by non-echo backends such as CopilotBridgeClient that already carry the
    // authored text. `author` is "user", "assistant", or "system"; anything else
    // is treated as "system". No-op on empty `text`.
    Q_INVOKABLE void appendBlock(const QString &author, const QString &text);

    // Read helpers for bindings and tests. Return empty on out-of-range.
    Q_INVOKABLE QString textAt(int row) const;
    Q_INVOKABLE QString authorAt(int row) const;

signals:
    void countChanged();

    // Transcript event stream, forwarded verbatim by a subscriber
    // (SessionMirrorServer). Emitted for every message the panel shows,
    // regardless of which backend produced it: the ChatModel is the single
    // source of truth, so the mirror stays in sync no matter the backend. An
    // atomic block (user input, or an external backend message) is one
    // messageAdded; a streamed reply is messageBegan / messageDelta* /
    // messageEnded. Ids are stable per block so a client can attribute streamed
    // deltas to the right author.
    void messageAdded(const QString &id, const QString &role, const QString &text);
    void messageBegan(const QString &id, const QString &role);
    void messageDelta(const QString &id, const QString &text);
    void messageEnded(const QString &id);

private:
    enum class Author {
        User,
        Assistant,
        System
    };

    struct Message {
        Author author;
        QString text;
    };

    // A queued streaming fragment carries its target row so a second submit()
    // arriving mid-stream can never append chunks to the wrong assistant block.
    // `id` is the assistant block's session id and `last` marks the final chunk
    // of a turn, so streamTick() can emit the matching delta / end taps.
    struct Chunk {
        int row;
        QString id;
        QString text;
        bool last;
    };

    void appendMessage(Author author, const QString &text);
    void streamTick();

    static QString authorName(Author a);
    static Author authorFromName(const QString &name);
    static QVector<QString> chunkify(const QString &s, int parts);

    QVector<Message> m_msgs;
    QQueue<Chunk> m_pending;
    QTimer *m_streamTimer = nullptr;
    quint64 m_turn = 0;  // per-turn id counter for session taps
    quint64 m_block = 0; // id counter for appendBlock() atomic-block taps
};

} // namespace qvim
