#ifndef CHATMODEL_H
#define CHATMODEL_H

#include <QAbstractListModel>
#include <qqmlregistration.h>
#include <QString>
#include <QVector>

namespace qvim {

// Front-end chat model: a list of typed message blocks (user / assistant). It is
// a passive transcript — the active backend (CopilotBridgeClient) pushes blocks
// in via appendBlock(); ChatModel itself drives no backend and fabricates no
// replies.
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

    // Appends an externally-sourced block verbatim (no echo, no streaming). Used
    // by the backend (CopilotBridgeClient), which already carries the authored
    // text. `author` is "user", "assistant", or "system"; anything else is
    // treated as "system". No-op on empty `text`.
    Q_INVOKABLE void appendBlock(const QString &author, const QString &text);

    // Read helpers for bindings and tests. Return empty on out-of-range.
    Q_INVOKABLE QString textAt(int row) const;
    Q_INVOKABLE QString authorAt(int row) const;

signals:
    void countChanged();

    // Transcript event stream, forwarded verbatim by a subscriber
    // (SessionMirrorServer). messageAdded fires for every atomic block appended
    // via appendBlock() — the path every current backend uses. The streamed
    // triad (messageBegan / messageDelta* / messageEnded) is the wire-protocol
    // hook for an incremental backend; it has no emitter today and awaits a
    // streaming producer (e.g. the ACP backend). Ids are stable per block so a
    // client can attribute streamed deltas to the right author.
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

    void appendMessage(Author author, const QString &text);

    static QString authorName(Author a);
    static Author authorFromName(const QString &name);

    QVector<Message> m_msgs;
    quint64 m_block = 0; // id counter for appendBlock() atomic-block taps
};

} // namespace qvim

#endif
