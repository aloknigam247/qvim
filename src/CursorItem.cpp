#include "CursorItem.h"

#include "GridModel.h"
#include "HighlightTable.h"
#include "NvimConnector.h"

#include <QFontDatabase>
#include <QGlyphRun>
#include <QList>
#include <QPainter>
#include <QPointF>
#include <QRawFont>

namespace qvim {

namespace {
// Same PUA detection used by GridItem. Qt 6's text shaper drops Private Use
// Area codepoints (QTBUG-116417), so block-mode cursor glyphs in PUA need the
// QRawFont + drawGlyphRun bypass below.
bool isPua(QChar c) {
    const ushort u = c.unicode();
    if (u >= 0xE000 && u <= 0xF8FF) return true;
    return c.isHighSurrogate() && u >= 0xDB80;
}
} // namespace

CursorItem::CursorItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setFlag(ItemHasContents, true);
    // Cursor item must never take focus — qml/CLAUDE.md mandates focus stay
    // on the long-lived baseGrid in Shell.qml so it survives Repeater rebuilds.
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
    // Transparent background so non-cursor pixels in the item composite the
    // grid below through. QQuickPaintedItem clears the dirty region to
    // fillColor before paint() runs, so update(rect) on the previous cursor
    // cell automatically clears it back to transparent.
    setFillColor(Qt::transparent);

    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_font.setPointSizeF(m_fontSize);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setHintingPreference(QFont::PreferFullHinting);
    m_fontName = m_font.family();

    m_clock.start();
    m_blink.notifyActivity(m_clock.elapsed());

    m_blinkTimer.setSingleShot(true);
    connect(&m_blinkTimer, &QTimer::timeout, this, &CursorItem::onBlinkTimeout);
}

CursorItem::~CursorItem() = default;

void CursorItem::setConnector(NvimConnector* c) {
    if (m_conn == c) return;
    if (m_conn) disconnect(m_conn, nullptr, this, nullptr);
    m_conn = c;
    if (m_conn) {
        connect(m_conn, &NvimConnector::flush, this, &CursorItem::onFlush);
        if (auto* g = grid()) {
            connect(g, &GridModel::cursorChanged, this, &CursorItem::onCursorActivity);
        }
        if (auto* h = hl()) {
            connect(h, &HighlightTable::changed, this, &CursorItem::scheduleRepaint);
        }
        if (auto* m = mode()) {
            connect(m, &ModeInfo::currentChanged, this, &CursorItem::onModeBlinkChanged);
            onModeBlinkChanged();
        }
    }
    emit connectorChanged();
    scheduleRepaint();
}

void CursorItem::setCellWidth(qreal v) {
    if (qFuzzyCompare(v, m_cellWidth)) return;
    m_cellWidth = v;
    emit metricsChanged();
    update();
}

void CursorItem::setCellHeight(qreal v) {
    if (qFuzzyCompare(v, m_cellHeight)) return;
    m_cellHeight = v;
    emit metricsChanged();
    update();
}

void CursorItem::setBaseline(qreal v) {
    if (qFuzzyCompare(v, m_baseline)) return;
    m_baseline = v;
    emit metricsChanged();
    update();
}

void CursorItem::setFontName(const QString& v) {
    if (v == m_fontName) return;
    m_fontName = v;
    rebuildFont();
    emit fontChanged();
    update();
}

void CursorItem::setFontSize(qreal v) {
    if (qFuzzyCompare(v, m_fontSize)) return;
    m_fontSize = v;
    rebuildFont();
    emit fontChanged();
    update();
}

void CursorItem::rebuildFont() {
    m_font.setFamily(m_fontName);
    m_font.setPointSizeF(m_fontSize);
}

GridModel*      CursorItem::grid() const { return m_conn ? m_conn->grid()       : nullptr; }
HighlightTable* CursorItem::hl()   const { return m_conn ? m_conn->highlights() : nullptr; }
ModeInfo*       CursorItem::mode() const { return m_conn ? m_conn->modeInfo()   : nullptr; }

QRectF CursorItem::cursorRectFor(int row, int col,
                                 qreal cellWidth, qreal cellHeight,
                                 CursorShape shape) {
    QRectF rect(col * cellWidth, row * cellHeight, cellWidth, cellHeight);
    // Preserve the existing 15% hardcode from src/GridItem.cpp. Mode info's
    // cellPercentage is intentionally ignored for parity — wiring it through
    // is a separate change that affects visual behaviour.
    if (shape == CursorShape::Vertical) {
        rect.setWidth(std::max(1.0, cellWidth * 0.15));
    } else if (shape == CursorShape::Horizontal) {
        rect.setY(rect.bottom() - std::max(1.0, cellHeight * 0.15));
        rect.setHeight(std::max(1.0, cellHeight * 0.15));
    }
    return rect;
}

QRectF CursorItem::currentCursorRect() const {
    GridModel* g = grid();
    if (!g) return {};
    const int active = g->activeGrid();
    auto* surface = g->surfaceFor(active);
    if (!surface || !surface->visible()) return {};

    const int gridCols = surface->cols();
    const int gridRows = surface->rows();
    const int localRow = g->cursorRowOf(active);
    const int localCol = g->cursorColOf(active);
    if (localRow < 0 || localRow >= gridRows ||
        localCol < 0 || localCol >= gridCols) {
        return {};
    }

    const int absRow = surface->y() + localRow;
    const int absCol = surface->x() + localCol;
    const CursorShape shape = mode()
        ? static_cast<CursorShape>(mode()->cursorShapeInt())
        : CursorShape::Block;
    return cursorRectFor(absRow, absCol, m_cellWidth, m_cellHeight, shape);
}

void CursorItem::scheduleRepaint() {
    const QRectF current = currentCursorRect();
    QRectF dirty = current;
    if (!m_lastRect.isNull()) {
        dirty = dirty.isNull() ? m_lastRect : dirty.united(m_lastRect);
    }
    if (dirty.isNull()) {
        update();
        return;
    }
    update(dirty.toAlignedRect());
}

void CursorItem::onCursorActivity() {
    m_blink.notifyActivity(m_clock.elapsed());
    rescheduleBlink();
    scheduleRepaint();
}

void CursorItem::onModeBlinkChanged() {
    if (auto* m = mode()) {
        m_blink.setBlinkParams(m->blinkWait(), m->blinkOn(), m->blinkOff());
    } else {
        m_blink.setBlinkParams(0, 0, 0);
    }
    m_blink.notifyActivity(m_clock.elapsed());
    rescheduleBlink();
    scheduleRepaint();
}

void CursorItem::onBlinkTimeout() {
    // Single-cell repaint — cursor cell only, no blink reset.
    const QRectF current = currentCursorRect();
    if (!current.isNull()) {
        update(current.toAlignedRect());
    }
    rescheduleBlink();
}

void CursorItem::onFlush() {
    // The cell under the cursor may have changed content (e.g. `r<char>` in
    // normal mode does not move the cursor). Repaint the cursor rect so the
    // block-mode glyph stays in sync with the underlying cell. One cell.
    const QRectF current = currentCursorRect();
    if (!current.isNull()) {
        update(current.toAlignedRect());
    }
}

void CursorItem::rescheduleBlink() {
    const qint64 now = m_clock.elapsed();
    const qint64 next = m_blink.nextChangeMs(now);
    if (next == CursorBlinkState::kNoChange) {
        m_blinkTimer.stop();
        return;
    }
    const qint64 delay = std::max<qint64>(1, next - now);
    m_blinkTimer.start(static_cast<int>(delay));
}

void CursorItem::paint(QPainter* painter) {
    GridModel* g = grid();
    HighlightTable* h = hl();
    if (!g || !h) {
        m_lastRect = {};
        return;
    }
    ModeInfo* m = mode();

    const int active = g->activeGrid();
    auto* surface = g->surfaceFor(active);
    if (!surface || !surface->visible()) {
        m_lastRect = {};
        return;
    }
    const int gridCols = surface->cols();
    const int gridRows = surface->rows();
    const int localRow = g->cursorRowOf(active);
    const int localCol = g->cursorColOf(active);
    if (localRow < 0 || localRow >= gridRows ||
        localCol < 0 || localCol >= gridCols) {
        m_lastRect = {};
        return;
    }

    const bool cursorOn = m_blink.isOn(m_clock.elapsed());
    const bool drawCursor = cursorOn || !m || !m->cursorStyleEnabled();
    if (!drawCursor) {
        // Blink-off phase: the dirty region was already cleared to fillColor
        // (transparent) by QQuickPaintedItem, so the grid below shows through.
        m_lastRect = {};
        return;
    }

    HlAttr a = h->resolved(m ? m->attrId() : 0);
    QColor curColor = a.bg.isValid() ? a.bg : h->defaultFg();
    if (curColor == h->defaultBg()) curColor = h->defaultFg();

    const CursorShape shape = m
        ? static_cast<CursorShape>(m->cursorShapeInt())
        : CursorShape::Block;
    const int absRow = surface->y() + localRow;
    const int absCol = surface->x() + localCol;
    const QRectF rect = cursorRectFor(absRow, absCol, m_cellWidth, m_cellHeight, shape);

    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->fillRect(rect, curColor);

    if (shape == CursorShape::Block && cursorOn) {
        const Cell& cell = g->cell(active, localRow, localCol);
        if (!cell.text.isEmpty()) {
            painter->setPen(h->defaultBg());
            painter->setFont(m_font);
            if (isPua(cell.text[0])) {
                // QTBUG-116417 workaround — Qt's shaper drops PUA codepoints,
                // so block-mode nerd-font cursor glyphs need the QRawFont +
                // drawGlyphRun bypass. Mirrors src/GridItem.cpp lines 603-614.
                const QRawFont raw = QRawFont::fromFont(m_font);
                const QList<quint32> ids = raw.glyphIndexesForString(cell.text);
                if (!ids.isEmpty()) {
                    QGlyphRun run;
                    run.setRawFont(raw);
                    run.setGlyphIndexes({ids.first()});
                    run.setPositions({QPointF(absCol * m_cellWidth,
                                              absRow * m_cellHeight + m_baseline)});
                    painter->drawGlyphRun(QPointF(0, 0), run);
                }
            } else {
                painter->drawText(QPointF(absCol * m_cellWidth,
                                          absRow * m_cellHeight + m_baseline),
                                  cell.text);
            }
        }
    }

    m_lastRect = rect;
}

} // namespace qvim
