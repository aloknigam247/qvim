#include "MessagesModel.h"

namespace qvim {

static QString asQString(const msgpack::object &o) {
    if(o.type != msgpack::type::STR) return {};
    return QString::fromUtf8(o.via.str.ptr, o.via.str.size);
}

MessagesModel::MessagesModel(QObject *parent) : QAbstractListModel(parent) {}

QString MessagesModel::joinText(const msgpack::object &content) {
    QString out;
    if(content.type != msgpack::type::ARRAY) return out;
    const auto &arr = content.via.array;
    for(uint32_t i = 0; i < arr.size; ++i) {
        const auto &chunk = arr.ptr[i];
        if(chunk.type != msgpack::type::ARRAY || chunk.via.array.size < 2) continue;
        const auto &textObj = chunk.via.array.ptr[1];
        if(textObj.type == msgpack::type::STR) {
            out += QString::fromUtf8(textObj.via.str.ptr, textObj.via.str.size);
        }
    }
    return out;
}

int MessagesModel::dominantAttr(const msgpack::object &content) {
    if(content.type != msgpack::type::ARRAY) return 0;
    const auto &arr = content.via.array;
    int best = 0;
    int bestLen = -1;
    for(uint32_t i = 0; i < arr.size; ++i) {
        const auto &chunk = arr.ptr[i];
        if(chunk.type != msgpack::type::ARRAY || chunk.via.array.size < 2) continue;
        const auto &attrObj = chunk.via.array.ptr[0];
        const auto &textObj = chunk.via.array.ptr[1];
        int attrId = 0;
        if(attrObj.type == msgpack::type::POSITIVE_INTEGER) {
            attrId = static_cast<int>(attrObj.via.u64);
        } else if(attrObj.type == msgpack::type::NEGATIVE_INTEGER) {
            attrId = static_cast<int>(attrObj.via.i64);
        }
        const int len =
            (textObj.type == msgpack::type::STR) ? static_cast<int>(textObj.via.str.size) : 0;
        if(len > bestLen) {
            bestLen = len;
            best = attrId;
        }
    }
    return best;
}

void MessagesModel::msgShow(const msgpack::object &kindObj, const msgpack::object &content,
                            bool replaceLast) {
    const QString kind = asQString(kindObj);
    const QString text = joinText(content);
    const int attr = dominantAttr(content);

    if(replaceLast && !m_items.isEmpty()) {
        const int last = static_cast<int>(m_items.size()) - 1;
        beginRemoveRows({}, last, last);
        m_items.pop_back();
        endRemoveRows();
    }

    const int row = static_cast<int>(m_items.size());
    beginInsertRows({}, row, row);
    m_items.push_back(Item{ kind, text, attr });
    endInsertRows();
    emit countChanged();
}

void MessagesModel::msgClear() {
    if(m_items.isEmpty()) return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

void MessagesModel::msgHistoryShow(const msgpack::object &entries) {
    if(entries.type != msgpack::type::ARRAY) return;
    const auto &arr = entries.via.array;
    beginResetModel();
    m_items.clear();
    m_items.reserve(arr.size);
    for(uint32_t i = 0; i < arr.size; ++i) {
        const auto &entry = arr.ptr[i];
        if(entry.type != msgpack::type::ARRAY || entry.via.array.size < 2) continue;
        const QString kind = asQString(entry.via.array.ptr[0]);
        const QString text = joinText(entry.via.array.ptr[1]);
        const int attr = dominantAttr(entry.via.array.ptr[1]);
        m_items.push_back(Item{ kind, text, attr });
    }
    endResetModel();
    emit countChanged();
}

void MessagesModel::msgShowMode(const msgpack::object &content) {
    const QString s = joinText(content);
    if(s == m_mode) return;
    m_mode = s;
    emit modeChanged();
}

void MessagesModel::msgShowCmd(const msgpack::object &content) {
    const QString s = joinText(content);
    if(s == m_cmd) return;
    m_cmd = s;
    emit cmdChanged();
}

void MessagesModel::msgRuler(const msgpack::object &content) {
    const QString s = joinText(content);
    if(s == m_ruler) return;
    m_ruler = s;
    emit rulerChanged();
}

void MessagesModel::reset() {
    if(!m_items.isEmpty()) {
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit countChanged();
    }
    if(!m_mode.isEmpty()) {
        m_mode.clear();
        emit modeChanged();
    }
    if(!m_cmd.isEmpty()) {
        m_cmd.clear();
        emit cmdChanged();
    }
    if(!m_ruler.isEmpty()) {
        m_ruler.clear();
        emit rulerChanged();
    }
}

int MessagesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant MessagesModel::data(const QModelIndex &index, int role) const {
    if(!index.isValid() || index.row() >= m_items.size()) return {};
    const Item &it = m_items[index.row()];
    switch(role) {
        case KindRole:
            return it.kind;
        case TextRole:
            return it.text;
        case AttrIdRole:
            return it.attrId;
        default:
            return {};
    }
}

QHash<int, QByteArray> MessagesModel::roleNames() const {
    return {
        { AttrIdRole, "attrId" },
        { KindRole, "kind" },
        { TextRole, "text" },
    };
}

} // namespace qvim
