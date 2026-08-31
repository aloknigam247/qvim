#include "PopupMenuModel.h"

namespace qvim {

static QString strOrEmpty(const msgpack::object &o) {
    if(o.type != msgpack::type::STR) return {};
    return QString::fromUtf8(o.via.str.ptr, o.via.str.size);
}

PopupMenuModel::PopupMenuModel(QObject *parent) : QAbstractListModel(parent) {}

void PopupMenuModel::show(const msgpack::object &items, int selected, int row, int col) {
    beginResetModel();
    m_items.clear();
    if(items.type == msgpack::type::ARRAY) {
        const auto &arr = items.via.array;
        m_items.reserve(arr.size);
        for(uint32_t i = 0; i < arr.size; ++i) {
            const auto &e = arr.ptr[i];
            if(e.type != msgpack::type::ARRAY || e.via.array.size < 4) continue;
            Item it{
                strOrEmpty(e.via.array.ptr[0]),
                strOrEmpty(e.via.array.ptr[1]),
                strOrEmpty(e.via.array.ptr[2]),
                strOrEmpty(e.via.array.ptr[3]),
            };
            m_items.push_back(it);
        }
    }
    endResetModel();

    m_selected = selected;
    m_row = row;
    m_col = col;
    if(!m_visible) {
        m_visible = true;
        emit visibilityChanged();
    }
    emit selectedChanged();
    emit anchorChanged();
}

void PopupMenuModel::select(int idx) {
    if(idx == m_selected) return;
    m_selected = idx;
    emit selectedChanged();
}

void PopupMenuModel::hide() {
    if(!m_visible) return;
    m_visible = false;
    emit visibilityChanged();
}

int PopupMenuModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant PopupMenuModel::data(const QModelIndex &index, int role) const {
    if(!index.isValid() || index.row() >= m_items.size()) return {};
    const Item &it = m_items[index.row()];
    switch(role) {
        case WordRole:
            return it.word;
        case KindRole:
            return it.kind;
        case MenuRole:
            return it.menu;
        case InfoRole:
            return it.info;
        default:
            return {};
    }
}

QHash<int, QByteArray> PopupMenuModel::roleNames() const {
    return {
        { WordRole, "word" },
        { KindRole, "kind" },
        { MenuRole, "menu" },
        { InfoRole, "info" },
    };
}

} // namespace qvim
