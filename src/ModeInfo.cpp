#include "ModeInfo.h"

namespace qvim {

namespace {
QString mapKeyStr(const msgpack::object &mapObj, const char *key) {
    if(mapObj.type != msgpack::type::MAP) return {};
    const auto &m = mapObj.via.map;
    for(uint32_t i = 0; i < m.size; ++i) {
        if(m.ptr[i].key.type != msgpack::type::STR) continue;
        const auto &s = m.ptr[i].key.via.str;
        if(s.size != std::strlen(key) || std::memcmp(s.ptr, key, s.size) != 0) continue;
        const auto &v = m.ptr[i].val;
        if(v.type == msgpack::type::STR) return QString::fromUtf8(v.via.str.ptr, v.via.str.size);
        return {};
    }
    return {};
}

int mapKeyInt(const msgpack::object &mapObj, const char *key, int def = 0) {
    if(mapObj.type != msgpack::type::MAP) return def;
    const auto &m = mapObj.via.map;
    for(uint32_t i = 0; i < m.size; ++i) {
        if(m.ptr[i].key.type != msgpack::type::STR) continue;
        const auto &s = m.ptr[i].key.via.str;
        if(s.size != std::strlen(key) || std::memcmp(s.ptr, key, s.size) != 0) continue;
        const auto &v = m.ptr[i].val;
        if(v.type == msgpack::type::POSITIVE_INTEGER) return static_cast<int>(v.via.u64);
        if(v.type == msgpack::type::NEGATIVE_INTEGER) return static_cast<int>(v.via.i64);
        return def;
    }
    return def;
}

CursorShape parseShape(const QString &s) {
    if(s == QStringLiteral("horizontal")) return CursorShape::Horizontal;
    if(s == QStringLiteral("vertical")) return CursorShape::Vertical;
    return CursorShape::Block;
}
} // namespace

ModeInfo::ModeInfo(QObject *parent) : QObject(parent) {}

void ModeInfo::setModes(const msgpack::object &info, bool cursorStyleEnabled) {
    m_cursorStyleEnabled = cursorStyleEnabled;
    m_modes.clear();
    if(info.type != msgpack::type::ARRAY) return;
    const auto &arr = info.via.array;
    m_modes.reserve(arr.size);
    for(uint32_t i = 0; i < arr.size; ++i) {
        const auto &e = arr.ptr[i];
        ModeDescriptor d;
        d.name = mapKeyStr(e, "name");
        d.shortName = mapKeyStr(e, "short_name");
        d.shape = parseShape(mapKeyStr(e, "cursor_shape"));
        d.cellPercentage = mapKeyInt(e, "cell_percentage", 100);
        d.blinkWait = mapKeyInt(e, "blinkwait", 0);
        d.blinkOn = mapKeyInt(e, "blinkon", 0);
        d.blinkOff = mapKeyInt(e, "blinkoff", 0);
        d.attrId = mapKeyInt(e, "attr_id", 0);
        m_modes.push_back(d);
    }
}

void ModeInfo::setCurrentMode(const QString &name, int idx) {
    if(idx >= 0 && idx < m_modes.size()) {
        m_current = m_modes[idx];
        if(m_current.name.isEmpty()) m_current.name = name;
    } else {
        m_current = {};
        m_current.name = name;
    }
    emit currentChanged();
}

} // namespace qvim
