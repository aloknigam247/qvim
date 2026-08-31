#ifndef MESSAGESMODEL_H
#define MESSAGESMODEL_H

#include <msgpack.hpp>
#include <QAbstractListModel>
#include <qqmlregistration.h>
#include <QString>
#include <QVector>

namespace qvim {

class MessagesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NvimConnector")

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString lastKind READ lastKind NOTIFY countChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString cmd READ cmd NOTIFY cmdChanged)
    Q_PROPERTY(QString ruler READ ruler NOTIFY rulerChanged)

public:
    enum Roles {
        KindRole = Qt::UserRole + 1,
        TextRole,
        AttrIdRole
    };

    explicit MessagesModel(QObject *parent = nullptr);

    // ui-messages protocol entry points
    void msgShow(const msgpack::object &kindObj, const msgpack::object &content, bool replaceLast);
    void msgClear();
    void msgHistoryShow(const msgpack::object &entries);
    void msgShowMode(const msgpack::object &content);
    void msgShowCmd(const msgpack::object &content);
    void msgRuler(const msgpack::object &content);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_items.size()); }
    QString lastKind() const { return m_items.isEmpty() ? QString() : m_items.back().kind; }
    QString mode() const { return m_mode; }
    QString cmd() const { return m_cmd; }
    QString ruler() const { return m_ruler; }

signals:
    void countChanged();
    void modeChanged();
    void cmdChanged();
    void rulerChanged();

private:
    struct Item {
        QString kind;
        QString text;
        int attrId = 0;
    };

    static QString joinText(const msgpack::object &content);
    static int dominantAttr(const msgpack::object &content);

    QVector<Item> m_items;
    QString m_mode;
    QString m_cmd;
    QString m_ruler;
};

} // namespace qvim

#endif
