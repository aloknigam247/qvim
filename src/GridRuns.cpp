#include "GridRuns.h"

#include "GridModel.h"
#include "HighlightTable.h"

namespace qvim {

bool isPuaChar(QChar c) {
    const ushort u = c.unicode();
    if(u >= 0xE000 && u <= 0xF8FF) return true;
    // Supplementary PUA-A (U+F0000-U+FFFFD) lives in surrogate range
    // U+DB80-U+DBBF; PUA-B (U+100000-U+10FFFD) in U+DBC0-U+DBFF.
    return c.isHighSurrogate() && u >= 0xDB80;
}

// Collect the rounded-highlight spans and the ambient background each one sits
// on. Sampling is deliberately O(1) per span — probe the two adjacent cells
// only — because this runs on every frame in the redraw path. `cellAt(c)`
// resolves a column of the current row, so the same logic serves both the live
// grid and a snapshotted row block.
template <class CellAt>
static void collectPillsRow(CellAt cellAt, const HighlightTable &hl, int r, int cols,
                            QVector<PillSpan> &out) {
    int c = 0;
    while(c < cols) {
        const Cell &cell = cellAt(c);
        if(!hl.isRounded(cell.hlId)) {
            ++c;
            continue;
        }

        const int c0 = c;
        const int spanHl = cell.hlId;
        const QColor spanBg = hl.resolved(spanHl).bg;
        while(c < cols && hl.isRounded(cellAt(c).hlId) && hl.resolved(cellAt(c).hlId).bg == spanBg)
            ++c;

        QColor backBg;
        if(c0 > 0) {
            const int leftHl = cellAt(c0 - 1).hlId;
            if(!hl.isRounded(leftHl)) backBg = hl.resolved(leftHl).bg;
        }
        if(!backBg.isValid() && c < cols) {
            const int rightHl = cellAt(c).hlId;
            if(!hl.isRounded(rightHl)) backBg = hl.resolved(rightHl).bg;
        }
        out.push_back({ r, c0, c, spanHl, backBg });
    }
}

// Split a row into PUA clusters. A cluster runs while consecutive cells share
// an hl_id and remain PUA, so each cluster needs a single colour and a single
// draw call. Double-width right halves are absorbed without breaking the run.
template <class CellAt>
static void collectPuaClustersRow(CellAt cellAt, int row, int cols, QVector<PuaCluster> &out) {
    int c = 0;
    while(c < cols) {
        const Cell &cell = cellAt(c);
        if(cell.doubleWidth || cell.text.isEmpty() || !isPuaChar(cell.text[0])) {
            ++c;
            continue;
        }

        const int clusterHl = cell.hlId;
        int end = c + 1;
        while(end < cols) {
            const Cell &next = cellAt(end);
            if(next.doubleWidth) {
                ++end;
                continue;
            }
            if(next.hlId != clusterHl) break;
            if(next.text.isEmpty() || !isPuaChar(next.text[0])) break;
            ++end;
        }
        out.push_back({ row, c, end, clusterHl });
        c = end;
    }
}

// Resolve one row into runs (+ its pills and PUA clusters). `cellAt(c)` gives
// the cell at column c of row `r`; the caller supplies it for the live grid or
// for a snapshotted row so both paths share identical batching behaviour.
template <class CellAt>
static void appendRowRuns(CellAt cellAt, const HighlightTable &hl, const QColor &defaultBg, int r,
                          int cols, GridRuns &out) {
    collectPillsRow(cellAt, hl, r, cols, out.pills);

    // Set while building this row's runs, so the cluster scan is skipped
    // entirely on PUA-free rows — the common case in code buffers.
    bool rowHasPua = false;

    int c = 0;
    while(c < cols) {
        const int runHl = cellAt(c).hlId;
        int runEnd = c + 1;
        while(runEnd < cols && cellAt(runEnd).hlId == runHl) ++runEnd;

        const HlAttr a = hl.resolved(runHl);

        CellRun run;
        run.row = r;
        run.c0 = c;
        run.c1 = runEnd;
        run.hlId = runHl;
        run.bg = a.bg;
        run.fg = a.fg;
        run.sp = a.sp;
        run.bold = a.bold;
        run.italic = a.italic;
        run.strikethrough = a.strikethrough;
        run.underline = a.underline;
        run.undercurl = a.undercurl;
        run.fillBg = a.bg != defaultBg && !hl.isRounded(runHl);

        run.text.reserve(runEnd - c);
        for(int cc = c; cc < runEnd; ++cc) {
            const Cell &cell = cellAt(cc);
            // Right-half markers of double-width glyphs contribute no glyph
            // of their own; the left cell's glyph already spans both columns.
            if(cell.doubleWidth) continue;
            if(cell.text.isEmpty()) {
                run.text += QChar(' ');
                continue;
            }
            // PUA cells are blanked out of the run text and drawn by the
            // cluster pass instead, which positions each one at its own x.
            // Leaving them in would make rendering depend on an unspecified
            // Qt shaper behaviour: whether it drops PUA codepoints or gives
            // them a zero advance. The former renders correctly by accident,
            // the latter double-draws the icon and shifts every later glyph
            // in the run. Substituting a space makes the outcome the same
            // either way, and the cluster pass covers every PUA cell.
            if(isPuaChar(cell.text[0])) {
                run.text += QChar(' ');
                rowHasPua = true;
                continue;
            }
            run.text += cell.text;
        }

        out.runs.push_back(std::move(run));
        c = runEnd;
    }

    if(rowHasPua) collectPuaClustersRow(cellAt, r, cols, out.puaClusters);
}

GridRuns buildGridRuns(const GridModel &grid, const HighlightTable &hl, int gridId) {
    GridRuns out;

    const int rows = grid.gridRows(gridId);
    const int cols = grid.gridCols(gridId);
    const QColor defaultBg = hl.defaultBg();
    if(rows <= 0 || cols <= 0) return out;

    // Worst case is one run per cell; reserving the row width keeps the common
    // case (a handful of runs per row) allocation-free after the first row.
    out.runs.reserve(static_cast<qsizetype>(rows) * 4);

    for(int r = 0; r < rows; ++r) {
        appendRowRuns([&](int c) -> const Cell & { return grid.cell(gridId, r, c); }, hl, defaultBg,
                      r, cols, out);
    }

    return out;
}

GridRuns buildRowsRuns(const QVector<QVector<Cell>> &cellRows, const HighlightTable &hl) {
    GridRuns out;

    if(cellRows.isEmpty()) return out;
    const int cols = static_cast<int>(cellRows.first().size());
    const QColor defaultBg = hl.defaultBg();
    if(cols <= 0) return out;

    out.runs.reserve(cellRows.size() * 4);

    for(int r = 0; r < cellRows.size(); ++r) {
        const QVector<Cell> &row = cellRows[r];
        appendRowRuns([&](int c) -> const Cell & { return row[c]; }, hl, defaultBg, r, cols, out);
    }

    return out;
}

} // namespace qvim
