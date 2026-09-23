#include "GridModel.h"

#include <algorithm>

namespace qvim {

GridModel::GridModel(QObject *parent) : QObject(parent) {
    // The global grid (id=1) always exists.
    m_grids.insert(1, GridSurface{});
}

GridSurface *GridModel::surface(int gridId) {
    auto it = m_grids.find(gridId);
    return it == m_grids.end() ? nullptr : &it.value();
}

const GridSurface *GridModel::surface(int gridId) const {
    auto it = m_grids.constFind(gridId);
    return it == m_grids.constEnd() ? nullptr : &it.value();
}

GridSurface &GridModel::ensure(int gridId) {
    auto it = m_grids.find(gridId);
    if(it == m_grids.end()) { it = m_grids.insert(gridId, GridSurface{}); }
    return it.value();
}

void GridModel::resize(int gridId, int cols, int rows) {
    const bool newGrid = !m_grids.contains(gridId);
    GridSurface &s = ensure(gridId);
    if(!newGrid && s.cols == cols && s.rows == rows) return;
    s.cols = cols;
    s.rows = rows;
    s.cellRows.resize(rows);
    for(auto &row: s.cellRows) { row.assign(cols, Cell{ QStringLiteral(" "), 0, false }); }
    s.dirty = true;
    emit sizeChanged();
}

void GridModel::clear(int gridId) {
    GridSurface *s = surface(gridId);
    if(!s) return;
    for(auto &row: s->cellRows) {
        for(auto &c: row) c = Cell{ QStringLiteral(" "), 0, false };
    }
    s->dirty = true;
}

void GridModel::applyLine(int gridId, int row, int colStart, const msgpack::object &cellsArr) {
    if(cellsArr.type != msgpack::type::ARRAY) return;
    GridSurface *s = surface(gridId);
    if(!s) return;
    if(row < 0 || row >= s->rows) return;

    s->dirty = true;
    int col = colStart;
    int lastHl = 0;
    // Tracks whether the most recently emitted cell carried a non-empty glyph
    // that occupies two display columns. The Neovim grid_line protocol emits an
    // entry with empty text immediately after such a glyph to mark the trailing
    // half; we translate that marker into doubleWidth=true here. Empty-text
    // entries that do not follow a non-empty cell are treated as legitimate
    // blanks (a single space, doubleWidth=false).
    bool prevWasNonEmpty = false;
    const auto &arr = cellsArr.via.array;
    for(uint32_t i = 0; i < arr.size; ++i) {
        const auto &entry = arr.ptr[i];
        if(entry.type != msgpack::type::ARRAY || entry.via.array.size < 1) continue;
        const auto &e = entry.via.array;

        QString text;
        if(e.ptr[0].type == msgpack::type::STR) {
            text = QString::fromUtf8(e.ptr[0].via.str.ptr, e.ptr[0].via.str.size);
        }

        int hl = lastHl;
        int repeat = 1;
        if(e.size >= 2 && e.ptr[1].type == msgpack::type::POSITIVE_INTEGER) {
            hl = static_cast<int>(e.ptr[1].via.u64);
            lastHl = hl;
        }
        if(e.size >= 3 && e.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
            repeat = static_cast<int>(e.ptr[2].via.u64);
        }

        const bool entryEmpty = text.isEmpty();
        QVector<Cell> &rowCells = s->cellRows[row];
        for(int r = 0; r < repeat; ++r) {
            if(col >= s->cols) break;
            if(entryEmpty) {
                if(prevWasNonEmpty) {
                    // Right half of a double-width glyph emitted by the previous cell.
                    rowCells[col] = Cell{ QStringLiteral(""), hl, true };
                    prevWasNonEmpty = false;
                } else {
                    // Standalone empty cell -> render as a real blank.
                    rowCells[col] = Cell{ QStringLiteral(" "), hl, false };
                }
            } else {
                rowCells[col] = Cell{ text, hl, false };
                prevWasNonEmpty = true;
            }
            ++col;
        }
    }
}

void GridModel::scroll(int gridId, int top, int bot, int left, int right, int rows) {
    if(rows == 0 || left >= right || top >= bot) return;
    GridSurface *s = surface(gridId);
    if(!s) return;
    s->dirty = true;

    // Fast path: a full-width scroll (left/right cover the whole grid) is what
    // j/k/Ctrl-D/Ctrl-U emit at 200x60. Rotating the QVector<Cell> row handles
    // is O(rows) implicit-share-pointer swaps regardless of cols, where the
    // per-cell loop was O(rows*cols) ref-count touches. Newly revealed rows
    // stay populated with their old contents; the grid_line events that
    // follow grid_scroll overwrite them with the correct cells.
    if(left == 0 && right == s->cols) {
        if(rows > 0) {
            std::rotate(s->cellRows.begin() + top, s->cellRows.begin() + top + rows,
                        s->cellRows.begin() + bot);
        } else {
            const int n = -rows;
            std::rotate(s->cellRows.begin() + top, s->cellRows.begin() + bot - n,
                        s->cellRows.begin() + bot);
        }
        return;
    }

    // Partial-width scroll (split window with sibling columns to the side):
    // we still need a per-cell copy within the [left, right) column band. Use
    // std::copy across full rows where possible — gives the compiler a chance
    // to vectorise the QString refcount touches.
    if(rows > 0) {
        for(int r = top; r < bot - rows; ++r) {
            const QVector<Cell> &src = s->cellRows[r + rows];
            QVector<Cell> &dst = s->cellRows[r];
            std::copy(src.begin() + left, src.begin() + right, dst.begin() + left);
        }
    } else {
        const int n = -rows;
        for(int r = bot - 1; r >= top + n; --r) {
            const QVector<Cell> &src = s->cellRows[r - n];
            QVector<Cell> &dst = s->cellRows[r];
            std::copy(src.begin() + left, src.begin() + right, dst.begin() + left);
        }
    }
}

void GridModel::setCursor(int gridId, int row, int col) {
    GridSurface *s = surface(gridId);
    if(!s) return;
    const bool sameCursor = (s->cursorRow == row && s->cursorCol == col);
    const bool sameActive = (m_active == gridId);
    if(sameCursor && sameActive) return;
    s->cursorRow = row;
    s->cursorCol = col;
    m_active = gridId;
    emit cursorChanged();
}

void GridModel::reset() {
    for(auto it = m_grids.begin(); it != m_grids.end();) {
        if(it.key() != 1) it = m_grids.erase(it);
        else ++it;
    }
    m_grids[1] = GridSurface{};
    m_active = 1;
    emit sizeChanged();
    emit cursorChanged();
}

int GridModel::gridCols(int gridId) const {
    const auto *s = surface(gridId);
    return s ? s->cols : 0;
}
int GridModel::gridRows(int gridId) const {
    const auto *s = surface(gridId);
    return s ? s->rows : 0;
}

const Cell &GridModel::cell(int gridId, int row, int col) const {
    static const Cell empty{ QStringLiteral(" "), 0, false };
    const GridSurface *s = surface(gridId);
    if(!s) return empty;
    if(row < 0 || row >= s->rows || col < 0 || col >= s->cols) return empty;
    return s->cellRows[row][col];
}

QString GridModel::dumpAscii(int gridId) const {
    const GridSurface *s = surface(gridId);
    if(!s) return {};
    QString out;
    out.reserve(static_cast<qsizetype>(s->rows) * (s->cols + 1));
    for(int r = 0; r < s->rows; ++r) {
        const QVector<Cell> &rowCells = s->cellRows[r];
        for(int c = 0; c < s->cols; ++c) {
            const Cell &cellRef = rowCells[c];
            out += cellRef.text.isEmpty() ? QChar(' ') : cellRef.text.at(0);
        }
        out += QChar('\n');
    }
    return out;
}

} // namespace qvim
