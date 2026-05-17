#pragma once

#include <QAbstractListModel>
#include <QString>
#include <qqmlregistration.h>
#include <msgpack.hpp>

namespace qvim {

class PopupMenuModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NvimConnector")
    Q_PROPERTY(bool visible       READ visible       NOTIFY visibilityChanged)
    Q_PROPERTY(int  selectedIndex READ selectedIndex NOTIFY selectedChanged)
    Q_PROPERTY(int  anchorRow     READ anchorRow     NOTIFY anchorChanged)
    Q_PROPERTY(int  anchorCol     READ anchorCol     NOTIFY anchorChanged)

public:
    enum Roles {
        WordRole = Qt::UserRole + 1,
        KindRole,
        MenuRole,
        InfoRole,
    };

    explicit PopupMenuModel(QObject* parent = nullptr);

    void show(const msgpack::object& items, int selected, int row, int col);
    void select(int idx);
    void hide();

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool visible() const { return m_visible; }
    int  selectedIndex() const { return m_selected; }
    int  anchorRow() const { return m_row; }
    int  anchorCol() const { return m_col; }

signals:
    void visibilityChanged();
    void selectedChanged();
    void anchorChanged();

private:
    struct Item { QString word; QString kind; QString menu; QString info; };
    QVector<Item> m_items;
    int  m_selected = -1;
    int  m_row = 0;
    int  m_col = 0;
    bool m_visible = false;
};

} // namespace qvim
