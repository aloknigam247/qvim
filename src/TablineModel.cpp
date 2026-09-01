#include "TablineModel.h"

namespace qvim {

static int64_t extInt(const msgpack::object &o) {
    if(o.type == msgpack::type::POSITIVE_INTEGER) return static_cast<int64_t>(o.via.u64);
    if(o.type == msgpack::type::NEGATIVE_INTEGER) return o.via.i64;
    if(o.type == msgpack::type::EXT) {
        msgpack::object_handle h;
        msgpack::unpack(h, o.via.ext.data(), o.via.ext.size);
        return extInt(h.get());
    }
    return 0;
}

TablineModel::TablineModel(QObject *parent) : QAbstractListModel(parent) {}

void TablineModel::update(const msgpack::object &current, const msgpack::object &tabs) {
    beginResetModel();
    m_tabs.clear();
    m_currentHandle = extInt(current);
    m_currentIndex = -1;

    if(tabs.type == msgpack::type::ARRAY) {
        const auto &arr = tabs.via.array;
        m_tabs.reserve(arr.size);
        for(uint32_t i = 0; i < arr.size; ++i) {
            const auto &entry = arr.ptr[i];
            if(entry.type != msgpack::type::MAP) continue;
            Tab t;
            for(uint32_t k = 0; k < entry.via.map.size; ++k) {
                const auto &kv = entry.via.map.ptr[k];
                if(kv.key.type != msgpack::type::STR) continue;
                const auto &s = kv.key.via.str;
                if(s.size == 4 && std::memcmp(s.ptr, "name", 4) == 0 &&
                   kv.val.type == msgpack::type::STR) {
                    t.name = QString::fromUtf8(kv.val.via.str.ptr, kv.val.via.str.size);
                } else if(s.size == 3 && std::memcmp(s.ptr, "tab", 3) == 0) {
                    t.handle = extInt(kv.val);
                }
            }
            if(t.handle == m_currentHandle) m_currentIndex = static_cast<int>(m_tabs.size());
            m_tabs.push_back(t);
        }
    }
    endResetModel();
    emit currentChanged();
}

int TablineModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_tabs.size());
}

QVariant TablineModel::data(const QModelIndex &index, int role) const {
    if(!index.isValid() || index.row() >= m_tabs.size()) return {};
    const Tab &t = m_tabs[index.row()];
    switch(role) {
        case NameRole:
            return t.name;
        case TabHandleRole:
            return static_cast<qlonglong>(t.handle);
        case CurrentRole:
            return t.handle == m_currentHandle;
        default:
            return {};
    }
}

QHash<int, QByteArray> TablineModel::roleNames() const {
    return {
        { NameRole, "name" },
        { TabHandleRole, "tabHandle" },
        { CurrentRole, "isCurrent" },
    };
}

} // namespace qvim
