#include "GridModel.h"

namespace qvim {

void GridSurfaceProxy::setPosition(int x, int y) {
    if (m_x == x && m_y == y) return;
    m_x = x;
    m_y = y;
    emit positionChanged();
}

void GridSurfaceProxy::setSize(int cols, int rows) {
    if (m_cols == cols && m_rows == rows) return;
    m_cols = cols;
    m_rows = rows;
    emit sizeChanged();
}

void GridSurfaceProxy::setVisible(bool v) {
    if (m_visible == v) return;
    m_visible = v;
    emit visibilityChanged();
}

void GridSurfaceProxy::setFloat(bool isFloat, int zindex) {
    if (m_isFloat == isFloat && m_zindex == zindex) return;
    m_isFloat = isFloat;
    m_zindex  = zindex;
    emit floatChanged();
}

GridModel::GridModel(QObject* parent) : QObject(parent) {
    // The global grid (id=1) always exists; its proxy is created lazily on
    // first ensure() call to keep ctor allocation-free.
    m_grids.insert(1, GridSurface{});
}

GridSurface* GridModel::surface(int gridId) {
    auto it = m_grids.find(gridId);
    return it == m_grids.end() ? nullptr : &it.value();
}

const GridSurface* GridModel::surface(int gridId) const {
    auto it = m_grids.constFind(gridId);
    return it == m_grids.constEnd() ? nullptr : &it.value();
}

GridSurface& GridModel::ensure(int gridId) {
    auto it = m_grids.find(gridId);
    if (it == m_grids.end()) {
        it = m_grids.insert(gridId, GridSurface{});
        ensureProxy(gridId);
        emit gridsChanged();
    }
    return it.value();
}

GridSurfaceProxy* GridModel::ensureProxy(int gridId) {
    auto it = m_proxies.find(gridId);
    if (it != m_proxies.end()) return it.value();
    auto* p = new GridSurfaceProxy(gridId, this);
    m_proxies.insert(gridId, p);
    return p;
}

GridSurfaceProxy* GridModel::surfaceFor(int gridId) const {
    auto it = m_proxies.constFind(gridId);
    if (it == m_proxies.constEnd()) {
        // Lazily materialise for any grid we already know about (e.g. grid 1
        // before its first ensure()). Const-cast is fine: proxies are caches
        // tied to logical grid lifetime, not part of the const-observable model
        // state from QML's perspective.
        if (m_grids.contains(gridId)) {
            return const_cast<GridModel*>(this)->ensureProxy(gridId);
        }
        return nullptr;
    }
    return it.value();
}

void GridModel::resize(int gridId, int cols, int rows) {
    const bool newGrid = !m_grids.contains(gridId);
    GridSurface& s = ensure(gridId);
    if (!newGrid && s.cols == cols && s.rows == rows) return;
    s.cols = cols;
    s.rows = rows;
    s.cells.assign(static_cast<qsizetype>(cols) * rows, Cell{QStringLiteral(" "), 0, false});
    ensureProxy(gridId)->setSize(cols, rows);
    emit sizeChanged();
}

void GridModel::clear(int gridId) {
    GridSurface* s = surface(gridId);
    if (!s) return;
    for (auto& c : s->cells) c = Cell{QStringLiteral(" "), 0, false};
}

void GridModel::applyLine(int gridId, int row, int colStart, const msgpack::object& cellsArr) {
    if (cellsArr.type != msgpack::type::ARRAY) return;
    GridSurface* s = surface(gridId);
    if (!s) return;
    if (row < 0 || row >= s->rows) return;

    int col = colStart;
    int lastHl = 0;
    // Tracks whether the most recently emitted cell carried a non-empty glyph
    // that occupies two display columns. The Neovim grid_line protocol emits an
    // entry with empty text immediately after such a glyph to mark the trailing
    // half; we translate that marker into doubleWidth=true here. Empty-text
    // entries that do not follow a non-empty cell are treated as legitimate
    // blanks (a single space, doubleWidth=false).
    bool prevWasNonEmpty = false;
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

        const bool entryEmpty = text.isEmpty();
        for (int r = 0; r < repeat; ++r) {
            if (col >= s->cols) break;
            const qsizetype idx = static_cast<qsizetype>(row) * s->cols + col;
            if (entryEmpty) {
                if (prevWasNonEmpty) {
                    // Right half of a double-width glyph emitted by the previous cell.
                    s->cells[idx] = Cell{QStringLiteral(""), hl, true};
                    prevWasNonEmpty = false;
                } else {
                    // Standalone empty cell -> render as a real blank.
                    s->cells[idx] = Cell{QStringLiteral(" "), hl, false};
                }
            } else {
                s->cells[idx] = Cell{text, hl, false};
                prevWasNonEmpty = true;
            }
            ++col;
        }
    }
}

void GridModel::scroll(int gridId, int top, int bot, int left, int right, int rows) {
    if (rows == 0 || left >= right || top >= bot) return;
    GridSurface* s = surface(gridId);
    if (!s) return;

    if (rows > 0) {
        for (int r = top; r < bot - rows; ++r) {
            for (int c = left; c < right; ++c) {
                const qsizetype dst = static_cast<qsizetype>(r) * s->cols + c;
                const qsizetype src = static_cast<qsizetype>(r + rows) * s->cols + c;
                s->cells[dst] = s->cells[src];
            }
        }
    } else {
        const int n = -rows;
        for (int r = bot - 1; r >= top + n; --r) {
            for (int c = left; c < right; ++c) {
                const qsizetype dst = static_cast<qsizetype>(r) * s->cols + c;
                const qsizetype src = static_cast<qsizetype>(r - n) * s->cols + c;
                s->cells[dst] = s->cells[src];
            }
        }
    }
}

void GridModel::setCursor(int gridId, int row, int col) {
    GridSurface* s = surface(gridId);
    if (!s) return;
    const bool sameCursor = (s->cursorRow == row && s->cursorCol == col);
    const bool sameActive = (m_active == gridId);
    if (sameCursor && sameActive) return;
    s->cursorRow = row;
    s->cursorCol = col;
    m_active = gridId;
    emit cursorChanged();
}

void GridModel::destroyGrid(int gridId) {
    if (gridId == 1) return;        // never destroy the global grid
    if (m_grids.remove(gridId) > 0) {
        if (m_active == gridId) m_active = 1;
        // Schedule the proxy for deletion via the event loop so any QML
        // delegate currently binding to it tears down its bindings first
        // (the gridsChanged() below makes the Repeater drop the delegate).
        if (auto it = m_proxies.find(gridId); it != m_proxies.end()) {
            it.value()->deleteLater();
            m_proxies.erase(it);
        }
        emit gridsChanged();
    }
}

void GridModel::setPos(int gridId, int x, int y, int w, int h) {
    GridSurface& s = ensure(gridId);
    s.x       = x;
    s.y       = y;
    s.visible = true;
    s.isFloat = false;
    if (w > 0 && h > 0 && (w != s.cols || h != s.rows)) {
        s.cols = w;
        s.rows = h;
        s.cells.assign(static_cast<qsizetype>(w) * h, Cell{QStringLiteral(" "), 0, false});
        emit sizeChanged();
    }
    auto* p = ensureProxy(gridId);
    p->setPosition(x, y);
    p->setSize(s.cols, s.rows);
    p->setFloat(false, s.zindex);
    p->setVisible(true);
    emit gridGeometryChanged(gridId);
}

void GridModel::setFloatPos(int gridId, int /*anchorGrid*/, int anchorRow, int anchorCol, int zindex) {
    GridSurface& s = ensure(gridId);
    // v1: collapse anchor to global-grid coords; full anchor resolution is
    // QML's job once it knows where the anchor grid sits.
    s.x       = anchorCol;
    s.y       = anchorRow;
    s.zindex  = zindex;
    s.visible = true;
    s.isFloat = true;
    auto* p = ensureProxy(gridId);
    p->setPosition(anchorCol, anchorRow);
    p->setFloat(true, zindex);
    p->setVisible(true);
    emit gridGeometryChanged(gridId);
}

void GridModel::setExternalPos(int gridId) {
    GridSurface& s = ensure(gridId);
    s.visible = true;
    s.isFloat = false;
    auto* p = ensureProxy(gridId);
    p->setFloat(false, s.zindex);
    p->setVisible(true);
    emit gridGeometryChanged(gridId);
}

void GridModel::setHidden(int gridId) {
    GridSurface* s = surface(gridId);
    if (!s) return;
    if (!s->visible) return;
    s->visible = false;
    if (auto* p = surfaceFor(gridId)) p->setVisible(false);
    emit gridGeometryChanged(gridId);
}

void GridModel::setViewport(int /*gridId*/, int /*topline*/, int /*botline*/, int /*curline*/, int /*curcol*/) {
    // v1: viewport info is currently informational only. Stored slot reserved
    // for scroll-anchored animations once we wire them up.
}

QList<int> GridModel::gridIds() const {
    QList<int> ids;
    ids.reserve(m_grids.size());
    for (auto it = m_grids.constBegin(); it != m_grids.constEnd(); ++it) {
        ids.push_back(it.key());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

QRect GridModel::gridGeometry(int gridId) const {
    const GridSurface* s = surface(gridId);
    if (!s) return {};
    return QRect(s->x, s->y, s->cols, s->rows);
}

int GridModel::gridCols(int gridId) const     { const auto* s = surface(gridId); return s ? s->cols    : 0; }
int GridModel::gridRows(int gridId) const     { const auto* s = surface(gridId); return s ? s->rows    : 0; }
bool GridModel::gridVisible(int gridId) const { const auto* s = surface(gridId); return s ? s->visible : false; }
bool GridModel::gridIsFloat(int gridId) const { const auto* s = surface(gridId); return s ? s->isFloat : false; }
int GridModel::gridZindex(int gridId) const   { const auto* s = surface(gridId); return s ? s->zindex  : 0; }

const Cell& GridModel::cell(int gridId, int row, int col) const {
    static const Cell empty{QStringLiteral(" "), 0, false};
    const GridSurface* s = surface(gridId);
    if (!s) return empty;
    if (row < 0 || row >= s->rows || col < 0 || col >= s->cols) return empty;
    return s->cells[static_cast<qsizetype>(row) * s->cols + col];
}

QString GridModel::dumpAscii(int gridId) const {
    const GridSurface* s = surface(gridId);
    if (!s) return {};
    QString out;
    out.reserve(static_cast<qsizetype>(s->rows) * (s->cols + 1));
    for (int r = 0; r < s->rows; ++r) {
        for (int c = 0; c < s->cols; ++c) {
            const Cell& cellRef = s->cells[static_cast<qsizetype>(r) * s->cols + c];
            out += cellRef.text.isEmpty() ? QChar(' ') : cellRef.text.at(0);
        }
        out += QChar('\n');
    }
    return out;
}

} // namespace qvim
