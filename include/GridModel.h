#ifndef GRIDMODEL_H
#define GRIDMODEL_H

#include <msgpack.hpp>
#include <QHash>
#include <QList>
#include <QObject>
#include <qqmlregistration.h>
#include <QRect>
#include <QString>
#include <QVector>

namespace qvim {

struct Cell {
    QString text;
    int hlId = 0;
    bool doubleWidth = false;
};

// Per-grid surface state. With ext_multigrid every window has its own grid;
// the global grid (id=1) always exists. Geometry (x,y,w,h) is in screen-cell
// coordinates relative to the global grid as reported by win_pos / win_float_pos.
struct GridSurface {
    int cols = 0;
    int rows = 0;
    int cursorRow = 0;
    int cursorCol = 0;
    int x = 0; // position in global cells
    int y = 0;
    int zindex = 0; // for floats
    bool visible = true;
    bool isFloat = false;
    // Non-float windows are always focusable; nvim reports the flag only on
    // win_float_pos. Default true so the QML delegate's MouseArea is enabled
    // unless explicitly turned off for an unfocusable float.
    bool focusable = true;
    // Row-of-rows storage. Picking row-pointer indirection over a flat
    // row-major QVector<Cell> turns grid_scroll into an O(rows) std::rotate
    // over QVector<Cell> handles (cheap implicit-share-pointer swap) instead
    // of an O(rows*cols) per-Cell copy. Holding j/k now pays one ref-count
    // touch per moved row rather than one per moved cell, which is the
    // dominant scroll cost at 200x60. Each inner vector is exactly `cols`
    // long; the `rows` int above is the count.
    QVector<QVector<Cell>> cellRows;
    // Set whenever cell content changes (applyLine, scroll, clear, resize).
    // GridItem::onFlush reads + clears this via takeDirty(gridId); if false,
    // the flush triggers no update() — so pure cursor moves (which produce
    // grid_cursor_goto + flush with no grid_line) skip the full row*col
    // paint loop. Initial value true to ensure the first paint after
    // construction runs.
    bool dirty = true;
};

// QObject wrapper exposing one grid's geometry as bindable properties. QML
// delegates bind to these properties directly so a win_pos / grid_resize on
// an existing grid re-evaluates the delegate's x/y/width/height bindings
// in-place without destroying the delegate (and thus without thrashing focus,
// blink state, or per-instance caches in GridItem).
class GridSurfaceProxy : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by GridModel")
    Q_PROPERTY(int gridId READ gridId CONSTANT)
    Q_PROPERTY(int x READ x NOTIFY positionChanged)
    Q_PROPERTY(int y READ y NOTIFY positionChanged)
    Q_PROPERTY(int cols READ cols NOTIFY sizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY sizeChanged)
    Q_PROPERTY(bool visible READ visible NOTIFY visibilityChanged)
    Q_PROPERTY(bool isFloat READ isFloat NOTIFY floatChanged)
    Q_PROPERTY(int zindex READ zindex NOTIFY floatChanged)
    Q_PROPERTY(bool isFocusable READ isFocusable NOTIFY focusableChanged)

public:
    explicit GridSurfaceProxy(int id, QObject *parent = nullptr) : QObject(parent), m_gridId(id) {}

    int gridId() const { return m_gridId; }
    int x() const { return m_x; }
    int y() const { return m_y; }
    int cols() const { return m_cols; }
    int rows() const { return m_rows; }
    bool visible() const { return m_visible; }
    bool isFloat() const { return m_isFloat; }
    int zindex() const { return m_zindex; }
    bool isFocusable() const { return m_isFocusable; }

    // Setters emit only when the value actually changes. Callers in GridModel
    // funnel every surface mutation through these so we don't re-paint or
    // re-bind on no-op updates from nvim's redraw stream.
    void setPosition(int x, int y);
    void setSize(int cols, int rows);
    void setVisible(bool v);
    void setFloat(bool isFloat, int zindex);
    void setFocusable(bool focusable);

signals:
    void positionChanged();
    void sizeChanged();
    void visibilityChanged();
    void floatChanged();
    void focusableChanged();

private:
    const int m_gridId;
    int m_x = 0;
    int m_y = 0;
    int m_cols = 0;
    int m_rows = 0;
    bool m_visible = true;
    bool m_isFloat = false;
    int m_zindex = 0;
    bool m_isFocusable = true;
};

class GridModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NvimConnector")
    // Active-grid passthrough — for back-compat with the pre-multigrid renderer
    // and tests, these properties expose the active grid (last grid_cursor_goto
    // target, defaulting to id=1).
    Q_PROPERTY(int cols READ cols NOTIFY sizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY sizeChanged)
    Q_PROPERTY(int cursorRow READ cursorRow NOTIFY cursorChanged)
    Q_PROPERTY(int cursorCol READ cursorCol NOTIFY cursorChanged)
    Q_PROPERTY(int activeGrid READ activeGrid NOTIFY cursorChanged)
    Q_PROPERTY(QList<int> gridIds READ gridIds NOTIFY gridsChanged)

public:
    explicit GridModel(QObject *parent = nullptr);

    // --- Multigrid API (gridId-aware) ---
    void resize(int gridId, int cols, int rows);
    void clear(int gridId);
    void applyLine(int gridId, int row, int colStart, const msgpack::object &cellsArr);
    void scroll(int gridId, int top, int bot, int left, int right, int rows);
    void setCursor(int gridId, int row, int col);
    void destroyGrid(int gridId);
    void setPos(int gridId, int x, int y, int w, int h);
    void setFloatPos(int gridId, int anchorGrid, int anchorRow, int anchorCol, bool focusable,
                     int zindex);
    void setExternalPos(int gridId);
    void setHidden(int gridId);
    void setViewport(int gridId, int topline, int botline, int curline, int curcol);

    // --- Single-grid API (kept for back-compat: delegates to grid 1) ---
    void resize(int cols, int rows) { resize(1, cols, rows); }
    void clear() { clear(1); }
    void applyLine(int row, int colStart, const msgpack::object &cellsArr) {
        applyLine(1, row, colStart, cellsArr);
    }
    void scroll(int top, int bot, int left, int right, int rows) {
        scroll(1, top, bot, left, right, rows);
    }
    void setCursor(int row, int col) { setCursor(1, row, col); }

    // --- Active-grid passthrough accessors ---
    int cols() const {
        const auto *s = surface(m_active);
        return s ? s->cols : 0;
    }
    int rows() const {
        const auto *s = surface(m_active);
        return s ? s->rows : 0;
    }
    int cursorRow() const {
        const auto *s = surface(m_active);
        return s ? s->cursorRow : 0;
    }
    int cursorCol() const {
        const auto *s = surface(m_active);
        return s ? s->cursorCol : 0;
    }
    int activeGrid() const { return m_active; }

    // --- Grid enumeration / lookup ---
    QList<int> gridIds() const;
    Q_INVOKABLE QRect gridGeometry(int gridId) const;
    Q_INVOKABLE int gridCols(int gridId) const;
    Q_INVOKABLE int gridRows(int gridId) const;
    Q_INVOKABLE bool gridVisible(int gridId) const;
    Q_INVOKABLE bool gridIsFloat(int gridId) const;
    Q_INVOKABLE bool gridIsFocusable(int gridId) const;
    Q_INVOKABLE int gridZindex(int gridId) const;
    // QML binds to the proxy's properties so geometry edits don't destroy the
    // delegate. Returns nullptr for unknown grids; QML handles that gracefully
    // (delegate stays invisible until the next gridsChanged rebuild).
    Q_INVOKABLE qvim::GridSurfaceProxy *surfaceFor(int gridId) const;
    bool hasGrid(int gridId) const { return m_grids.contains(gridId); }

    // Cell accessors
    const Cell &cell(int gridId, int row, int col) const;
    const Cell &cell(int row, int col) const { return cell(m_active, row, col); }
    int cursorRowOf(int gridId) const {
        const auto *s = surface(gridId);
        return s ? s->cursorRow : 0;
    }
    int cursorColOf(int gridId) const {
        const auto *s = surface(gridId);
        return s ? s->cursorCol : 0;
    }

    // Returns the grid's dirty flag and clears it. Called by GridItem::onFlush
    // to decide whether the flush actually needs a repaint. Cursor-only nvim
    // events (grid_cursor_goto + flush, common when holding j/k inside the
    // visible area at fullscreen) leave the grid clean — the CursorItem
    // overlay handles its own per-cell repaint, and the grid item stays
    // unchanged. Without this guard, every keystroke triggered a full
    // rows*cols paint loop, dominating frame time at large grid sizes.
    bool takeDirty(int gridId) {
        auto *s = surface(gridId);
        if(!s) return false;
        const bool wasDirty = s->dirty;
        s->dirty = false;
        return wasDirty;
    }
    // Peek-only variant. CursorItem checks this on cursorChanged (before
    // flush) to decide whether the move is part of a content change like
    // scroll/paste — in which case the cursor snaps instead of animating,
    // because eased motion relative to text that just moved looks wrong.
    bool isDirty(int gridId) const {
        const auto *s = surface(gridId);
        return s && s->dirty;
    }

    QString dumpAscii() const { return dumpAscii(m_active); }
    QString dumpAscii(int gridId) const;

signals:
    void sizeChanged();    // any grid resized (also fires for active grid changes)
    void cursorChanged();  // active grid or per-grid cursor moved
    void contentChanged(); // emitted on flush by NvimConnector
    void gridsChanged();   // grid added/removed
    void gridGeometryChanged(int gridId);

private:
    GridSurface *surface(int gridId);
    const GridSurface *surface(int gridId) const;
    GridSurface &ensure(int gridId);
    GridSurfaceProxy *ensureProxy(int gridId);

    QHash<int, GridSurface> m_grids;
    QHash<int, GridSurfaceProxy *> m_proxies; // owned; deleteLater'd on destroyGrid
    int m_active = 1;                         // grid id of the active cursor target
};

} // namespace qvim

#endif
