#include "CmdlineModel.h"

namespace qvim {

CmdlineModel::CmdlineModel(QObject* parent) : QObject(parent) {}

QString CmdlineModel::joinContent(const msgpack::object& contentArr) {
    QString out;
    if (contentArr.type != msgpack::type::ARRAY) return out;
    const auto& arr = contentArr.via.array;
    for (uint32_t i = 0; i < arr.size; ++i) {
        const auto& chunk = arr.ptr[i];
        if (chunk.type != msgpack::type::ARRAY || chunk.via.array.size < 2) continue;
        const auto& textObj = chunk.via.array.ptr[1];
        if (textObj.type == msgpack::type::STR) {
            out += QString::fromUtf8(textObj.via.str.ptr, textObj.via.str.size);
        }
    }
    return out;
}

void CmdlineModel::show(const msgpack::object& contentArr, int pos, const QString& firstchar,
                       const QString& prompt, int indent, int level) {
    m_content   = joinContent(contentArr);
    m_cursorPos = pos;
    m_firstChar = firstchar;
    m_prompt    = prompt;
    m_indent    = indent;
    m_level     = level;
    if (!m_visible) {
        m_visible = true;
        emit visibilityChanged();
    }
    emit contentChanged();
}

void CmdlineModel::setPos(int pos, int level) {
    m_cursorPos = pos;
    m_level     = level;
    emit contentChanged();
}

void CmdlineModel::setSpecialChar(const QString& c, bool /*shift*/, int level) {
    m_content = c;
    m_level   = level;
    emit contentChanged();
}

void CmdlineModel::hide() {
    if (!m_visible) return;
    m_visible = false;
    m_content.clear();
    m_cursorPos = 0;
    emit visibilityChanged();
    emit contentChanged();
}

void CmdlineModel::blockShow(const msgpack::object& lines) {
    m_blockLines.clear();
    if (lines.type == msgpack::type::ARRAY) {
        const auto& arr = lines.via.array;
        m_blockLines.reserve(arr.size);
        for (uint32_t i = 0; i < arr.size; ++i) m_blockLines.push_back(joinContent(arr.ptr[i]));
    }
    emit blockChanged();
}

void CmdlineModel::blockAppend(const msgpack::object& line) {
    m_blockLines.push_back(joinContent(line));
    emit blockChanged();
}

void CmdlineModel::blockHide() {
    if (m_blockLines.isEmpty()) return;
    m_blockLines.clear();
    emit blockChanged();
}

} // namespace qvim
