#ifndef TABLINEMODEL_H
#define TABLINEMODEL_H

#include <msgpack.hpp>
#include <QAbstractListModel>
#include <qqmlregistration.h>
#include <QString>

namespace qvim {

class TablineModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NvimConnector")
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        TabHandleRole,
        CurrentRole
    };

    explicit TablineModel(QObject *parent = nullptr);

    void update(const msgpack::object &current, const msgpack::object &tabs);
    // Clears all tabs and the current selection. Used on :restart.
    void clear();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const { return m_currentIndex; }

signals:
    void currentChanged();

private:
    struct Tab {
        QString name;
        int64_t handle = 0;
    };
    QVector<Tab> m_tabs;
    int64_t m_currentHandle = 0;
    int m_currentIndex = -1;
};

} // namespace qvim

#endif
