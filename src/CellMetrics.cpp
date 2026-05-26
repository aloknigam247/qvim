#include "CellMetrics.h"

#include <algorithm>
#include <cmath>

namespace qvim {

CellMetrics computeCellMetrics(const QFontMetricsF& fm, int linespace) {
    // Snap cell dimensions to integer pixels. Fractional cellWidth/cellHeight
    // place adjacent cell rects on sub-pixel boundaries, so antialiasing
    // bleeds the default background between selected rows/cols and produces
    // visible horizontal seams in visual-mode selections. Integer-valued
    // metrics guarantee adjacent cells share an exact pixel edge.
    const qreal extra  = static_cast<qreal>(std::max(0, linespace));
    const qreal width  = std::max<qreal>(1.0, std::round(fm.horizontalAdvance(QLatin1Char('M'))));
    const qreal height = std::max<qreal>(1.0, std::round(fm.height() + extra));
    qreal baseline     = std::round(fm.ascent() + extra / 2.0);
    if (baseline < 0.0)      baseline = 0.0;
    if (baseline > height)   baseline = height;
    return CellMetrics{width, height, baseline};
}

} // namespace qvim
