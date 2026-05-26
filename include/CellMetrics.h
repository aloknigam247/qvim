#pragma once

#include <QFontMetricsF>
#include <QtGlobal>

namespace qvim {

struct CellMetrics {
    qreal cellWidth;
    qreal cellHeight;
    qreal baseline;
};

CellMetrics computeCellMetrics(const QFontMetricsF& fm, int linespace);

} // namespace qvim
