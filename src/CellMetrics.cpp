#include "CellMetrics.h"

#include <algorithm>
#include <cmath>

namespace qvim {

CellMetrics computeCellMetrics(const QFontMetricsF &fm, int linespace, qreal dpr) {
    // Snap cell dimensions to a unit that is integer in BOTH logical and
    // device pixel space. For an integer DPR (1.0, 2.0) any integer logical
    // value already lands on an integer device pixel, so the unit is 1. For
    // a fractional DPR p/q in reduced form (e.g. 1.25 = 5/4, 1.5 = 3/2),
    // cell sizes must be multiples of q logical pixels for `cols*cellWidth`
    // to be an integer in both pixel spaces — otherwise the window pixel
    // size (always integer device pixels) cannot equal `cols * cellWidth`
    // and visible padding/gap appears at the grid edge (a previous version
    // of this function snapped to whole device pixels, which made cellWidth
    // fractional in logical pixels and broke window sizing).
    const qreal safeDpr = (dpr > 0.0) ? dpr : 1.0;
    const qreal extra = static_cast<qreal>(std::max(0, linespace));
    int unit = 1;
    for(int q = 1; q <= 16; ++q) {
        if(std::abs(safeDpr * q - std::round(safeDpr * q)) < 1e-3) {
            unit = q;
            break;
        }
    }
    auto snapUnit = [unit](qreal v) -> qreal {
        const qreal u = static_cast<qreal>(unit);
        return std::max(u, std::round(v / u) * u);
    };
    const qreal width = snapUnit(fm.horizontalAdvance(QLatin1Char('M')));
    const qreal height = snapUnit(fm.height() + extra);
    qreal baseline = std::round(fm.ascent() + extra / 2.0);
    if(baseline < 0.0) baseline = 0.0;
    if(baseline > height) baseline = height;
    return CellMetrics{ .cellWidth = width, .cellHeight = height, .baseline = baseline };
}

} // namespace qvim
