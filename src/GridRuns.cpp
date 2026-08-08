#include "GridRuns.h"

#include "GridModel.h"
#include "HighlightTable.h"

namespace qvim {

bool isPuaChar(QChar c) {
    const ushort u = c.unicode();
    if (u >= 0xE000 && u <= 0xF8FF) return true;
    // Supplementary PUA-A (U+F0000-U+FFFFD) lives in surrogate range
    // U+DB80-U+DBBF; PUA-B (U+100000-U+10FFFD) in U+DBC0-U+DBFF.
    return c.isHighSurrogate() && u >= 0xDB80;
}

namespace {

// Collect the rounded-highlight spans and the ambient background each one sits
// on. Sampling is deliberately O(1) per span — probe the two adjacent cells
// only — because this runs on every frame in the redraw path.
void collectPills(const GridModel& grid, const HighlightTable& hl, int gridId,
                  int rows, int cols, QVector<PillSpan>& out) {
    for (int r = 0; r < rows; ++r) {
        int c = 0;
        while (c < cols) {
            const Cell& cell = grid.cell(gridId, r, c);
            if (!hl.isRounded(cell.hlId)) { ++c; continue; }

            const int    c0     = c;
            const int    spanHl = cell.hlId;
            const QColor spanBg = hl.resolved(spanHl).bg;
            while (c < cols && hl.isRounded(grid.cell(gridId, r, c).hlId)
                   && hl.resolved(grid.cell(gridId, r, c).hlId).bg == spanBg) ++c;

            QColor backBg;
            if (c0 > 0) {
                const int leftHl = grid.cell(gridId, r, c0 - 1).hlId;
                if (!hl.isRounded(leftHl)) backBg = hl.resolved(leftHl).bg;
            }
            if (!backBg.isValid() && c < cols) {
                const int rightHl = grid.cell(gridId, r, c).hlId;
                if (!hl.isRounded(rightHl)) backBg = hl.resolved(rightHl).bg;
            }
            out.push_back({r, c0, c, spanHl, backBg});
        }
    }
}

// Split a row into PUA clusters. A cluster runs while consecutive cells share
// an hl_id and remain PUA, so each cluster needs a single colour and a single
// draw call. Double-width right halves are absorbed without breaking the run.
void collectPuaClusters(const GridModel& grid, int gridId, int row, int cols,
                        QVector<PuaCluster>& out) {
    int c = 0;
    while (c < cols) {
        const Cell& cell = grid.cell(gridId, row, c);
        if (cell.doubleWidth || cell.text.isEmpty() || !isPuaChar(cell.text[0])) {
            ++c;
            continue;
        }

        const int clusterHl = cell.hlId;
        int end = c + 1;
        while (end < cols) {
            const Cell& next = grid.cell(gridId, row, end);
            if (next.doubleWidth) { ++end; continue; }
            if (next.hlId != clusterHl) break;
            if (next.text.isEmpty() || !isPuaChar(next.text[0])) break;
            ++end;
        }
        out.push_back({row, c, end, clusterHl});
        c = end;
    }
}

} // namespace

GridRuns buildGridRuns(const GridModel& grid, const HighlightTable& hl, int gridId) {
    GridRuns out;

    const int    rows      = grid.gridRows(gridId);
    const int    cols      = grid.gridCols(gridId);
    const QColor defaultBg = hl.defaultBg();
    if (rows <= 0 || cols <= 0) return out;

    collectPills(grid, hl, gridId, rows, cols, out.pills);

    // Worst case is one run per cell; reserving the row width keeps the common
    // case (a handful of runs per row) allocation-free after the first row.
    out.runs.reserve(rows * 4);

    for (int r = 0; r < rows; ++r) {
        int c = 0;
        while (c < cols) {
            const int runHl = grid.cell(gridId, r, c).hlId;
            int runEnd = c + 1;
            while (runEnd < cols && grid.cell(gridId, r, runEnd).hlId == runHl) ++runEnd;

            const HlAttr a = hl.resolved(runHl);

            CellRun run;
            run.row  = r;
            run.c0   = c;
            run.c1   = runEnd;
            run.hlId = runHl;
            run.bg   = a.bg;
            run.fg   = a.fg;
            run.sp   = a.sp;
            run.bold          = a.bold;
            run.italic        = a.italic;
            run.strikethrough = a.strikethrough;
            run.underline     = a.underline;
            run.undercurl     = a.undercurl;
            run.fillBg        = a.bg != defaultBg && !hl.isRounded(runHl);

            run.text.reserve(runEnd - c);
            for (int cc = c; cc < runEnd; ++cc) {
                const Cell& cell = grid.cell(gridId, r, cc);
                // Right-half markers of double-width glyphs contribute no glyph
                // of their own; the left cell's glyph already spans both columns.
                if (cell.doubleWidth) continue;
                if (cell.text.isEmpty()) run.text += QChar(' ');
                else                     run.text += cell.text;
            }

            out.runs.push_back(std::move(run));
            c = runEnd;
        }

        collectPuaClusters(grid, gridId, r, cols, out.puaClusters);
    }

    return out;
}

} // namespace qvim
