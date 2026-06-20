#include "CursorItem.h"

#include "GridModel.h"
#include "HighlightTable.h"
#include "NvimConnector.h"

#include <cstdlib>

#include <QEasingCurve>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QList>
#include <QPainter>
#include <QPointF>
#include <QRawFont>
#include <QVariant>

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

    // Move animation: 80ms ease-out, position-only. valueChanged drives
    // m_animatedPos and unions the swept rect into the next update() so
    // both the old and new cells (and everything between, on a single tick)
    // are redrawn. Stops on its own when the curve reaches t=1.
    m_moveAnim.setDuration(80);
    m_moveAnim.setEasingCurve(QEasingCurve::OutExpo);
    connect(&m_moveAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) {
                m_animatedPos = v.toPointF();
                scheduleRepaint();
            });
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
    return cursorRectAtPixel(QPointF(col * cellWidth, row * cellHeight),
                             cellWidth, cellHeight, shape);
}

QRectF CursorItem::cursorRectAtPixel(QPointF cellTopLeft,
                                     qreal cellWidth, qreal cellHeight,
                                     CursorShape shape) {
    QRectF rect(cellTopLeft.x(), cellTopLeft.y(), cellWidth, cellHeight);
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

std::pair<bool, QPointF> CursorItem::targetCellTopLeft() const {
    GridModel* g = grid();
    if (!g) return {false, {}};
    const int active = g->activeGrid();
    auto* surface = g->surfaceFor(active);
    if (!surface || !surface->visible()) return {false, {}};
    const int localRow = g->cursorRowOf(active);
    const int localCol = g->cursorColOf(active);
    if (localRow < 0 || localRow >= surface->rows() ||
        localCol < 0 || localCol >= surface->cols()) {
        return {false, {}};
    }
    const int absRow = surface->y() + localRow;
    const int absCol = surface->x() + localCol;
    return {true, QPointF(absCol * m_cellWidth, absRow * m_cellHeight)};
}

QRectF CursorItem::currentCursorRect() const {
    if (!m_hasAnimatedPos) return {};
    const CursorShape shape = mode()
        ? static_cast<CursorShape>(mode()->cursorShapeInt())
        : CursorShape::Block;
    return cursorRectAtPixel(m_animatedPos, m_cellWidth, m_cellHeight, shape);
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

    const auto [ok, target] = targetCellTopLeft();
    if (!ok) {
        m_moveAnim.stop();
        m_hasAnimatedPos = false;
        m_prevRow = -1;
        m_prevCol = -1;
        scheduleRepaint();
        return;
    }

    // Compute absolute target row/col (same math targetCellTopLeft() uses,
    // re-derived here so we can compare against m_prevRow/m_prevCol). Safe:
    // targetCellTopLeft() already verified surface + bounds.
    GridModel* g = grid();
    const int active = g->activeGrid();
    auto* surface = g->surfaceFor(active);
    const int newRow = surface->y() + g->cursorRowOf(active);
    const int newCol = surface->x() + g->cursorColOf(active);

    // Snap (no animation) when this is the first cursor we've seen OR when
    // the underlying cells changed in the same batch — scroll / paste /
    // anything where text moved alongside the cursor. Eased motion relative
    // to scrolling text looks like the cursor is fighting the page.
    const bool gridDirty = g && g->isDirty(g->activeGrid());
    const bool firstSeen = !m_hasAnimatedPos || m_prevRow < 0 || m_prevCol < 0;

    // Distance gate: only ease for 1-cell adjacent moves. Longer jumps (gg,
    // G, /search, %, H/M/L, ctrl-d/u, click-far) snap so we never see the
    // cursor slide across many lines/columns at once.
    bool farJump = false;
    if (!firstSeen) {
        const int dRow = std::abs(newRow - m_prevRow);
        const int dCol = std::abs(newCol - m_prevCol);
        farJump = (dRow > 1) || (dCol > 1);
    }

    const bool snap = firstSeen || gridDirty || farJump;

    if (snap) {
        m_moveAnim.stop();
        m_animatedPos = target;
        m_hasAnimatedPos = true;
    } else if (target != m_animatedPos) {
        // Same-content move (j/k/h/l with no scroll, w/b/e, click) — ease in.
        // Restart from the CURRENT animated position so a fast j-then-j chain
        // doesn't ping-pong: each new keystroke continues from where the
        // last animation got to, not from the previous target.
        m_moveAnim.stop();
        m_moveAnim.setStartValue(m_animatedPos);
        m_moveAnim.setEndValue(target);
        m_moveAnim.start();
    }
    // else: target == current animated position; nothing to animate, paint
    // will redraw at the same spot (covers blink-reset and same-cell mode
    // transitions).

    m_prevRow = newRow;
    m_prevCol = newCol;
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
    const int localRow = g->cursorRowOf(active);
    const int localCol = g->cursorColOf(active);
    if (localRow < 0 || localRow >= surface->rows() ||
        localCol < 0 || localCol >= surface->cols()) {
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

    // Visible pixel position. Once an animation has been seeded (i.e. we've
    // seen at least one cursorChanged on this CursorItem), m_animatedPos
    // sweeps between cells. Before then — typically the very first paint
    // after attach, because nvim's initial grid_cursor_goto fires before
    // CursorItem is constructed — fall back to the target cell so the
    // cursor is visible on launch.
    QPointF drawPos;
    if (m_hasAnimatedPos) {
        drawPos = m_animatedPos;
    } else {
        const int absRow = surface->y() + localRow;
        const int absCol = surface->x() + localCol;
        drawPos = QPointF(absCol * m_cellWidth, absRow * m_cellHeight);
    }
    const QRectF rect = cursorRectAtPixel(drawPos, m_cellWidth, m_cellHeight, shape);

    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->fillRect(rect, curColor);

    if (shape == CursorShape::Block && cursorOn) {
        // Glyph is the TARGET cell's text — the cursor "carries" its
        // destination glyph as it slides into place. Matches Goneovim.
        const Cell& cell = g->cell(active, localRow, localCol);
        if (!cell.text.isEmpty()) {
            // Preserve the target cell's font style (italic/bold/underline/
            // strikethrough) so the carry glyph matches how it renders in
            // the grid below. Without this, an italic word would lose its
            // slant under the block cursor.
            const HlAttr cellAttr = h->resolved(cell.hlId);
            QFont carryFont = m_font;
            carryFont.setItalic(cellAttr.italic);
            carryFont.setWeight(cellAttr.bold ? QFont::Bold : QFont::Normal);
            carryFont.setStrikeOut(cellAttr.strikethrough);
            carryFont.setUnderline(cellAttr.underline);
            painter->setPen(h->defaultBg());
            painter->setFont(carryFont);
            const qreal glyphX = drawPos.x();
            const qreal glyphY = drawPos.y() + m_baseline;
            if (isPua(cell.text[0])) {
                // QTBUG-116417 workaround — Qt's shaper drops PUA codepoints,
                // so block-mode nerd-font cursor glyphs need the QRawFont +
                // drawGlyphRun bypass.
                const QRawFont raw = QRawFont::fromFont(carryFont);
                const QList<quint32> ids = raw.glyphIndexesForString(cell.text);
                if (!ids.isEmpty()) {
                    QGlyphRun run;
                    run.setRawFont(raw);
                    run.setGlyphIndexes({ids.first()});
                    run.setPositions({QPointF(glyphX, glyphY)});
                    painter->drawGlyphRun(QPointF(0, 0), run);
                }
            } else {
                painter->drawText(QPointF(glyphX, glyphY), cell.text);
            }
        }
    }

    m_lastRect = rect;
}

} // namespace qvim
