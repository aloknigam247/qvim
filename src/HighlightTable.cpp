#include "HighlightTable.h"

namespace qvim {

namespace {
QColor fromRgb24(int rgb) {
    if (rgb < 0) return {};
    return QColor::fromRgb((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

int findKey(const msgpack::object& mapObj, const char* key) {
    if (mapObj.type != msgpack::type::MAP) return -1;
    const auto& m = mapObj.via.map;
    for (uint32_t i = 0; i < m.size; ++i) {
        if (m.ptr[i].key.type == msgpack::type::STR) {
            const auto& s = m.ptr[i].key.via.str;
            if (s.size == std::strlen(key) && std::memcmp(s.ptr, key, s.size) == 0) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

bool getBool(const msgpack::object& mapObj, const char* key) {
    const int i = findKey(mapObj, key);
    if (i < 0) return false;
    const auto& v = mapObj.via.map.ptr[i].val;
    return v.type == msgpack::type::BOOLEAN && v.via.boolean;
}

int getInt(const msgpack::object& mapObj, const char* key, int def = -1) {
    const int i = findKey(mapObj, key);
    if (i < 0) return def;
    const auto& v = mapObj.via.map.ptr[i].val;
    if (v.type == msgpack::type::POSITIVE_INTEGER) return static_cast<int>(v.via.u64);
    if (v.type == msgpack::type::NEGATIVE_INTEGER) return static_cast<int>(v.via.i64);
    return def;
}
} // namespace

HighlightTable::HighlightTable(QObject* parent) : QObject(parent) {}

void HighlightTable::setDefaultColors(int rgbFg, int rgbBg, int rgbSp) {
    if (rgbFg >= 0) m_defaultFg = fromRgb24(rgbFg);
    if (rgbBg >= 0) m_defaultBg = fromRgb24(rgbBg);
    if (rgbSp >= 0) m_defaultSp = fromRgb24(rgbSp);
    emit changed();
}

void HighlightTable::defineAttr(int id, const msgpack::object& rgbAttr) {
    HlAttr a;
    const int fg = getInt(rgbAttr, "foreground");
    const int bg = getInt(rgbAttr, "background");
    const int sp = getInt(rgbAttr, "special");
    if (fg >= 0) a.fg = fromRgb24(fg);
    if (bg >= 0) a.bg = fromRgb24(bg);
    if (sp >= 0) a.sp = fromRgb24(sp);
    a.bold          = getBool(rgbAttr, "bold");
    a.italic        = getBool(rgbAttr, "italic");
    a.underline     = getBool(rgbAttr, "underline");
    a.undercurl     = getBool(rgbAttr, "undercurl");
    a.strikethrough = getBool(rgbAttr, "strikethrough");
    a.reverse       = getBool(rgbAttr, "reverse");
    a.blend         = getInt(rgbAttr, "blend", 0);
    m_attrs[id] = a;
    emit changed();
}

void HighlightTable::clear() {
    m_attrs.clear();
    emit changed();
}

HlAttr HighlightTable::attr(int id) const {
    auto it = m_attrs.find(id);
    if (it == m_attrs.end()) return {};
    return it->second;
}

HlAttr HighlightTable::resolved(int id) const {
    HlAttr a = attr(id);
    if (!a.fg.isValid()) a.fg = m_defaultFg;
    if (!a.bg.isValid()) a.bg = m_defaultBg;
    if (!a.sp.isValid()) a.sp = m_defaultSp;
    if (a.reverse) std::swap(a.fg, a.bg);
    return a;
}

} // namespace qvim
