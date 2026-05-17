#include "GridModel.h"

namespace qvim {

GridModel::GridModel(QObject* parent) : QObject(parent) {}

void GridModel::resize(int cols, int rows) {
    if (cols == m_cols && rows == m_rows) return;
    m_cols = cols;
    m_rows = rows;
    m_cells.assign(static_cast<qsizetype>(cols) * rows, Cell{QStringLiteral(" "), 0, false});
    emit sizeChanged();
}

void GridModel::clear() {
    for (auto& c : m_cells) c = Cell{QStringLiteral(" "), 0, false};
}

void GridModel::applyLine(int row, int colStart, const msgpack::object& cellsArr) {
    if (cellsArr.type != msgpack::type::ARRAY) return;
    if (row < 0 || row >= m_rows) return;

    int col = colStart;
    int lastHl = 0;
    const auto& arr = cellsArr.via.array;
    for (uint32_t i = 0; i < arr.size; ++i) {
        const auto& entry = arr.ptr[i];
        if (entry.type != msgpack::type::ARRAY || entry.via.array.size < 1) continue;
        const auto& e = entry.via.array;

        QString text;
        if (e.ptr[0].type == msgpack::type::STR) {
            text = QString::fromUtf8(e.ptr[0].via.str.ptr, e.ptr[0].via.str.size);
        }

        int hl = lastHl;
        int repeat = 1;
        if (e.size >= 2 && e.ptr[1].type == msgpack::type::POSITIVE_INTEGER) {
            hl = static_cast<int>(e.ptr[1].via.u64);
            lastHl = hl;
        }
        if (e.size >= 3 && e.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
            repeat = static_cast<int>(e.ptr[2].via.u64);
        }

        const bool doubleWidth = text.isEmpty();   // zero-width cell = right half of double-width
        for (int r = 0; r < repeat; ++r) {
            if (col >= m_cols) break;
            const qsizetype idx = static_cast<qsizetype>(row) * m_cols + col;
            m_cells[idx] = Cell{text.isEmpty() ? QStringLiteral("") : text, hl, doubleWidth};
            ++col;
        }
    }
}

void GridModel::scroll(int top, int bot, int left, int right, int rows) {
    if (rows == 0 || left >= right || top >= bot) return;

    if (rows > 0) {
        for (int r = top; r < bot - rows; ++r) {
            for (int c = left; c < right; ++c) {
                const qsizetype dst = static_cast<qsizetype>(r) * m_cols + c;
                const qsizetype src = static_cast<qsizetype>(r + rows) * m_cols + c;
                m_cells[dst] = m_cells[src];
            }
        }
    } else {
        const int n = -rows;
        for (int r = bot - 1; r >= top + n; --r) {
            for (int c = left; c < right; ++c) {
                const qsizetype dst = static_cast<qsizetype>(r) * m_cols + c;
                const qsizetype src = static_cast<qsizetype>(r - n) * m_cols + c;
                m_cells[dst] = m_cells[src];
            }
        }
    }
}

void GridModel::setCursor(int row, int col) {
    if (row == m_cursorRow && col == m_cursorCol) return;
    m_cursorRow = row;
    m_cursorCol = col;
    emit cursorChanged();
}

const Cell& GridModel::cell(int row, int col) const {
    static const Cell empty{QStringLiteral(" "), 0, false};
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols) return empty;
    return m_cells[static_cast<qsizetype>(row) * m_cols + col];
}

QString GridModel::dumpAscii() const {
    QString out;
    out.reserve(static_cast<qsizetype>(m_rows) * (m_cols + 1));
    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            const Cell& cellRef = m_cells[static_cast<qsizetype>(r) * m_cols + c];
            out += cellRef.text.isEmpty() ? QChar(' ') : cellRef.text.at(0);
        }
        out += QChar('\n');
    }
    return out;
}

} // namespace qvim
