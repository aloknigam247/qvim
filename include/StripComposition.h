#ifndef STRIPCOMPOSITION_H
#define STRIPCOMPOSITION_H

#include <QtGlobal>
#include <QVector>

namespace qvim {

// One row of the scroll-transition strip. `baseY` is the pixel top of the row
// in the grid's coordinate space BEFORE the animated offset is applied — the
// eased offset is added by a single transform node at paint time, so it must
// not be baked in here.
struct StripRow {
    bool fromLost; // true: index into the snapshotted outgoing rows; false: absolute model row.
    int index;
    qreal baseY;
};

// Placements for the composite strip that reproduces a correct scroll over the
// region [top, bot) with `delta` (nvim grid_scroll semantics: delta > 0 means
// content moved up, so the outgoing rows are the |delta| rows that left the
// top; delta < 0 means content moved down, outgoing rows left the bottom).
//
// Pure function of the geometry — no snapshot data, so it is trivially
// unit-testable for row identity and full, single-source coverage of the band.
inline QVector<StripRow> stripRowPlacements(int delta, int top, int bot, qreal cellHeight) {
    QVector<StripRow> rows;
    if(delta == 0 || top >= bot) return rows;
    const int n = delta > 0 ? delta : -delta;
    rows.reserve((bot - top) + n);
    if(delta > 0) {
        // Outgoing rows sit above the final content; strip order lost ++ final.
        for(int k = 0; k < n; ++k) rows.push_back({ true, k, (k - n) * cellHeight });
        for(int r = top; r < bot; ++r) rows.push_back({ false, r, r * cellHeight });
    } else {
        // Outgoing rows sit below the final content; strip order final ++ lost.
        for(int r = top; r < bot; ++r) rows.push_back({ false, r, r * cellHeight });
        for(int k = 0; k < n; ++k) rows.push_back({ true, k, (bot + k) * cellHeight });
    }
    return rows;
}

} // namespace qvim

#endif
