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
#include <QQuickWindow>
#include <QRawFont>
#include <QVariant>

namespace qvim {

CursorItem::CursorItem(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    // Cursor item must never take focus — qml/AGENTS.md mandates focus stay
    // on the long-lived baseGrid in Shell.qml so it survives Repeater rebuilds.
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
    // The item emits geometry only where the cursor is; everywhere else it
    // contributes no nodes at all, so the grid below composites through.

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
    connect(&m_moveAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_animatedPos = v.toPointF();
        scheduleRepaint();
    });
}

CursorItem::~CursorItem() = default;

void CursorItem::setConnector(NvimConnector *c) {
    if(m_conn == c) return;
    if(m_conn) disconnect(m_conn, nullptr, this, nullptr);
    m_conn = c;
    if(m_conn) {
        connect(m_conn, &NvimConnector::flush, this, &CursorItem::onFlush);
        if(auto *g = grid()) {
            connect(g, &GridModel::cursorChanged, this, &CursorItem::onCursorActivity);
        }
        if(auto *h = hl()) {
            connect(h, &HighlightTable::changed, this, &CursorItem::scheduleRepaint);
        }
        if(auto *m = mode()) {
            connect(m, &ModeInfo::currentChanged, this, &CursorItem::onModeBlinkChanged);
            onModeBlinkChanged();
        }
    }
    emit connectorChanged();
    scheduleRepaint();
}

void CursorItem::setCellWidth(qreal v) {
    if(qFuzzyCompare(v, m_cellWidth)) return;
    m_cellWidth = v;
    emit metricsChanged();
    update();
}

void CursorItem::setCellHeight(qreal v) {
    if(qFuzzyCompare(v, m_cellHeight)) return;
    m_cellHeight = v;
    emit metricsChanged();
    update();
}

void CursorItem::setBaseline(qreal v) {
    if(qFuzzyCompare(v, m_baseline)) return;
    m_baseline = v;
    emit metricsChanged();
    update();
}

void CursorItem::setFontName(const QString &v) {
    if(v == m_fontName) return;
    m_fontName = v;
    rebuildFont();
    emit fontChanged();
    update();
}

void CursorItem::setFontSize(qreal v) {
    if(qFuzzyCompare(v, m_fontSize)) return;
    m_fontSize = v;
    rebuildFont();
    emit fontChanged();
    update();
}

void CursorItem::rebuildFont() {
    m_font.setFamily(m_fontName);
    m_font.setPointSizeF(m_fontSize);
}

GridModel *CursorItem::grid() const { return m_conn ? m_conn->grid() : nullptr; }
HighlightTable *CursorItem::hl() const { return m_conn ? m_conn->highlights() : nullptr; }
ModeInfo *CursorItem::mode() const { return m_conn ? m_conn->modeInfo() : nullptr; }

QRectF CursorItem::cursorRectFor(int row, int col, qreal cellWidth, qreal cellHeight,
                                 CursorShape shape) {
    return cursorRectAtPixel(QPointF(col * cellWidth, row * cellHeight), cellWidth, cellHeight,
                             shape);
}

QRectF CursorItem::cursorRectAtPixel(QPointF cellTopLeft, qreal cellWidth, qreal cellHeight,
                                     CursorShape shape) {
    QRectF rect(cellTopLeft.x(), cellTopLeft.y(), cellWidth, cellHeight);
    // Preserve the existing 15% hardcode from src/GridItem.cpp. Mode info's
    // cellPercentage is intentionally ignored for parity — wiring it through
    // is a separate change that affects visual behaviour.
    if(shape == CursorShape::Vertical) {
        rect.setWidth(std::max(1.0, cellWidth * 0.15));
    } else if(shape == CursorShape::Horizontal) {
        rect.setY(rect.bottom() - std::max(1.0, cellHeight * 0.15));
        rect.setHeight(std::max(1.0, cellHeight * 0.15));
    }
    return rect;
}

std::pair<bool, QPointF> CursorItem::targetCellTopLeft() const {
    GridModel *g = grid();
    if(!g) return { false, {} };
    const int active = g->activeGrid();
    auto *surface = g->surfaceFor(active);
    if(!surface || !surface->visible()) return { false, {} };
    const int localRow = g->cursorRowOf(active);
    const int localCol = g->cursorColOf(active);
    if(localRow < 0 || localRow >= surface->rows() || localCol < 0 || localCol >= surface->cols()) {
        return { false, {} };
    }
    const int absRow = surface->y() + localRow;
    const int absCol = surface->x() + localCol;
    return { true, QPointF(absCol * m_cellWidth, absRow * m_cellHeight) };
}

QRectF CursorItem::currentCursorRect() const {
    if(!m_hasAnimatedPos) return {};
    const CursorShape shape =
        mode() ? static_cast<CursorShape>(mode()->cursorShapeInt()) : CursorShape::Block;
    return cursorRectAtPixel(m_animatedPos, m_cellWidth, m_cellHeight, shape);
}

void CursorItem::scheduleRepaint() {
    // QQuickItem has no partial-invalidation entry point, and it does not need
    // one: the item's whole output is a couple of quads plus one glyph, so a
    // full rebuild is cheaper than tracking dirty rects was under
    // QQuickPaintedItem (where a partial update still re-rasterised a texture).
    update();
}

void CursorItem::onCursorActivity() {
    m_blink.notifyActivity(m_clock.elapsed());
    rescheduleBlink();

    const auto [ok, target] = targetCellTopLeft();
    if(!ok) {
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
    GridModel *g = grid();
    const int active = g->activeGrid();
    auto *surface = g->surfaceFor(active);
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
    if(!firstSeen) {
        const int dRow = std::abs(newRow - m_prevRow);
        const int dCol = std::abs(newCol - m_prevCol);
        farJump = (dRow > 1) || (dCol > 1);
    }

    const bool snap = firstSeen || gridDirty || farJump;

    if(snap) {
        m_moveAnim.stop();
        m_animatedPos = target;
        m_hasAnimatedPos = true;
    } else if(target != m_animatedPos) {
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
    if(auto *m = mode()) {
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
    update();
    rescheduleBlink();
}

void CursorItem::onFlush() {
    // The cell under the cursor may have changed content (e.g. `r<char>` in
    // normal mode does not move the cursor). Repaint so the block-mode glyph
    // stays in sync with the underlying cell.
    update();
}

void CursorItem::rescheduleBlink() {
    const qint64 now = m_clock.elapsed();
    const qint64 next = m_blink.nextChangeMs(now);
    if(next == CursorBlinkState::kNoChange) {
        m_blinkTimer.stop();
        return;
    }
    const qint64 delay = std::max<qint64>(1, next - now);
    m_blinkTimer.start(static_cast<int>(delay));
}

QSGNode *CursorItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
    // A null oldNode means the scene graph destroyed the previous tree, so the
    // cached slot pointers are dangling. Drop them without deleting.
    if(!oldNode) {
        m_blockRoot = m_textRoot = m_lineRoot = nullptr;
        m_blockPool.forget();
        m_linePool.forget();
        m_textPool.forget();
    }

    QSGNode *root = oldNode;
    if(!root) {
        root = new QSGNode;
        // Three fixed slots, created once: the cursor block, the carried glyph
        // above it, then the underline on top. Same layering GridItem uses, so
        // a descender crossing an underline looks the same under the cursor as
        // it does in the grid.
        m_blockRoot = new QSGNode;
        m_textRoot = new QSGNode;
        m_lineRoot = new QSGNode;
        root->appendChildNode(m_blockRoot);
        root->appendChildNode(m_textRoot);
        root->appendChildNode(m_lineRoot);
    }

    m_blockPool.beginFrame(m_blockRoot, window());
    m_linePool.beginFrame(m_lineRoot, window());
    m_textPool.beginFrame(m_textRoot, window());

    // Every early return still has to commit an empty frame, otherwise the
    // previous frame's cursor stays on screen.
    const auto commit = [&] {
        m_blockPool.endFrame();
        m_linePool.endFrame();
        m_textPool.endFrame();
        return root;
    };

    GridModel *g = grid();
    HighlightTable *h = hl();
    if(!g || !h) {
        m_lastRect = {};
        return commit();
    }
    ModeInfo *m = mode();

    const int active = g->activeGrid();
    auto *surface = g->surfaceFor(active);
    if(!surface || !surface->visible()) {
        m_lastRect = {};
        return commit();
    }
    const int localRow = g->cursorRowOf(active);
    const int localCol = g->cursorColOf(active);
    if(localRow < 0 || localRow >= surface->rows() || localCol < 0 || localCol >= surface->cols()) {
        m_lastRect = {};
        return commit();
    }

    const bool cursorOn = m_blink.isOn(m_clock.elapsed());
    const bool drawCursor = cursorOn || !m || !m->cursorStyleEnabled();
    if(!drawCursor) {
        m_lastRect = {};
        return commit();
    }

    HlAttr a = h->resolved(m ? m->attrId() : 0);
    QColor curColor = a.bg.isValid() ? a.bg : h->defaultFg();
    if(curColor == h->defaultBg()) curColor = h->defaultFg();

    const CursorShape shape =
        m ? static_cast<CursorShape>(m->cursorShapeInt()) : CursorShape::Block;

    // Visible pixel position. Once an animation has been seeded (i.e. we've
    // seen at least one cursorChanged on this CursorItem), m_animatedPos
    // sweeps between cells. Before then — typically the very first frame
    // after attach, because nvim's initial grid_cursor_goto fires before
    // CursorItem is constructed — fall back to the target cell so the
    // cursor is visible on launch.
    QPointF drawPos;
    if(m_hasAnimatedPos) {
        drawPos = m_animatedPos;
    } else {
        const int absRow = surface->y() + localRow;
        const int absCol = surface->x() + localCol;
        drawPos = QPointF(absCol * m_cellWidth, absRow * m_cellHeight);
    }
    const QRectF rect = cursorRectAtPixel(drawPos, m_cellWidth, m_cellHeight, shape);

    m_blockPool.add(rect, curColor);

    if(shape == CursorShape::Block && cursorOn) {
        // Glyph is the TARGET cell's text — the cursor "carries" its
        // destination glyph as it slides into place. Matches Goneovim.
        const Cell &cell = g->cell(active, localRow, localCol);
        if(!cell.text.isEmpty()) {
            // Preserve the target cell's font style (italic/bold/strikethrough)
            // so the carry glyph matches how it renders in the grid below.
            // Without this, an italic word would lose its slant under the
            // block cursor.
            const HlAttr cellAttr = h->resolved(cell.hlId);
            QFont carryFont = m_font;
            carryFont.setItalic(cellAttr.italic);
            carryFont.setWeight(cellAttr.bold ? QFont::Bold : QFont::Normal);
            carryFont.setStrikeOut(cellAttr.strikethrough);

            // A single cell at an explicit x, so the PUA zero-advance defect
            // (QTBUG-116417) that collapses multi-glyph runs cannot bite here:
            // nothing is ever positioned relative to this glyph.
            m_textPool.addText(0, cell.text, carryFont, h->defaultBg(),
                               QPointF(drawPos.x(), drawPos.y() + m_baseline));

            if(cellAttr.underline) {
                // The cursor block above overwrote the grid's underline at this
                // cell, so redraw it on top of the block.
                const qreal thickness =
                    std::max(1.0, std::round(QFontMetricsF(carryFont).lineWidth()));
                m_linePool.add(QRectF(drawPos.x(), drawPos.y() + m_cellHeight - thickness,
                                      m_cellWidth, thickness),
                               h->defaultBg());
            }
        }
    }

    m_lastRect = rect;
    return commit();
}

} // namespace qvim
