#ifndef GRIDRUNS_H
#define GRIDRUNS_H

#include <QColor>
#include <QString>
#include <QVector>

namespace qvim {

struct Cell;
class GridModel;
class HighlightTable;

// One horizontal stretch of cells sharing a single hl_id, in row-major order.
//
// Everything the renderer needs to draw the stretch is resolved up front so the
// draw pass never touches GridModel or HighlightTable again. Runs are emitted
// left-to-right within a row and rows top-to-bottom, and the painter consumes
// them in exactly that order — background, glyphs, then decorations per run —
// which is what makes the extraction behaviour-preserving.
struct CellRun {
    int row = 0;
    int c0 = 0; // first column, inclusive
    int c1 = 0; // one past the last column
    int hlId = 0;
    QString text; // glyphs for the run; double-width right halves omitted

    // bg differs from the default AND the run is not part of a rounded pill
    // (pills paint their own background in a separate pass).
    bool fillBg = false;
    QColor bg;
    QColor fg;
    QColor sp; // undercurl colour

    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    bool underline = false;
    bool undercurl = false;
};

// A maximal horizontal stretch of cells flagged isRounded that share a
// background, i.e. one row's slice of a rounded-highlight pill.
struct PillSpan {
    int row = 0;
    int c0 = 0;
    int c1 = 0;
    int hlId = 0;
    // Background of the line this span sits in, sampled from the nearest
    // adjacent non-rounded cell. Invalid when there is nothing to inherit
    // (the span touches both edges, or both neighbours are rounded too).
    QColor backBg;
};

// A stretch of consecutive Private Use Area cells sharing an hl_id.
//
// Qt's shaper assigns PUA codepoints a zero advance, so they collapse when laid
// out as a run. These clusters are therefore drawn by a separate pass that
// positions every glyph explicitly.
struct PuaCluster {
    int row = 0;
    int c0 = 0;
    int c1 = 0;
    int hlId = 0;
};

struct GridRuns {
    QVector<CellRun> runs;
    QVector<PillSpan> pills;
    QVector<PuaCluster> puaClusters;
};

// True for BMP Private Use Area (U+E000-U+F8FF) and for the high surrogate that
// starts a supplementary PUA-A (U+F0000-U+FFFFD) or PUA-B (U+100000-U+10FFFD)
// pair.
bool isPuaChar(QChar c);

// Walk one grid and resolve it into draw-ready runs. Pure with respect to
// (GridModel, HighlightTable, gridId) — no painter, no window, no font, which
// is what makes it unit-testable headlessly.
GridRuns buildGridRuns(const GridModel &grid, const HighlightTable &hl, int gridId);

// Same resolution over an explicit block of rows (each exactly `cols` wide),
// used to render the snapshotted outgoing rows of a smooth scroll. `row` on
// every emitted run/cluster/span is the index into `cellRows`. Shares the
// per-row logic with buildGridRuns so the two never drift.
GridRuns buildRowsRuns(const QVector<QVector<Cell>> &cellRows, const HighlightTable &hl);

} // namespace qvim

#endif
