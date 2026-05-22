#include "GridItem.h"
#include "NvimConnector.h"
#include "FontFallback.h"
#include "GridModel.h"
#include "HighlightTable.h"
#include "ModeInfo.h"
#include "InputHandler.h"

#include <QElapsedTimer>
#include <QGlyphRun>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QRegularExpression>
#include <QVarLengthArray>
#include <QVector>
#include <QWheelEvent>
#include <QtGlobal>
#include <climits>

namespace qvim {

namespace {
void parseGuifont(const QString& guifont, QString& family, qreal& size) {
    if (guifont.isEmpty()) return;
    const auto parts = guifont.split(QLatin1Char(':'));
    if (parts.isEmpty()) return;
    family = parts.first();
    family.replace(QLatin1Char('_'), QLatin1Char(' '));
    for (int i = 1; i < parts.size(); ++i) {
        const QString p = parts.at(i);
        if (p.startsWith(QLatin1Char('h')) && p.size() > 1) {
            bool ok = false;
            const qreal v = p.mid(1).toDouble(&ok);
            if (ok) size = v;
        }
    }
}
} // namespace

GridItem::~GridItem() = default;

GridItem::GridItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(false);
    setFlag(ItemHasContents, true);
    setFlag(ItemIsFocusScope, true);
    setActiveFocusOnTab(true);
    m_font = QFont(m_fontName, m_fontSize);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setHintingPreference(QFont::PreferFullHinting);
    m_fallback = std::make_unique<FontFallback>();
    recomputeMetrics();
    connect(&m_blinkTimer, &QTimer::timeout, this, &GridItem::blinkTick);
    m_blinkTimer.setInterval(500);
    m_blinkTimer.start();
}

void GridItem::setGridId(int id) {
    if (m_gridId == id) return;
    m_gridId = id;
    emit gridIdChanged();
    update();
}

void GridItem::setConnector(NvimConnector* c) {
    if (m_conn == c) return;
    if (m_conn) disconnect(m_conn, nullptr, this, nullptr);
    m_conn = c;
    if (m_conn) {
        connect(m_conn, &NvimConnector::guifontChanged, this, &GridItem::onGuifontChanged);
        connect(m_conn, &NvimConnector::flush,          this, &GridItem::onFlush);
        if (auto* g = grid()) {
            connect(g, &GridModel::sizeChanged,   this, [this]{ update(); });
            connect(g, &GridModel::cursorChanged, this, [this]{ update(); });
        }
        if (auto* h = hl()) {
            connect(h, &HighlightTable::changed, this, [this]{ update(); });
        }
        if (auto* m = mode()) {
            connect(m, &ModeInfo::currentChanged, this, [this]{ m_cursorOn = true; update(); });
        }
    }
    emit connectorChanged();
    update();
}

void GridItem::setFontName(const QString& name) {
    if (name == m_fontName) return;
    m_fontName = name;
    m_font.setFamily(name);
    recomputeMetrics();
    emit fontChanged();
    maybeResizeUi();
    update();
}

void GridItem::setFontSize(qreal pt) {
    if (qFuzzyCompare(pt, m_fontSize)) return;
    m_fontSize = pt;
    m_font.setPointSizeF(pt);
    recomputeMetrics();
    emit fontChanged();
    maybeResizeUi();
    update();
}

void GridItem::setDebugOverlay(bool v) {
    if (v == m_debugOverlay) return;
    m_debugOverlay = v;
    emit debugOverlayChanged();
    update();
}

void GridItem::recomputeMetrics() {
    const QFontMetricsF fm(m_font);
    m_cellWidth  = fm.horizontalAdvance(QLatin1Char('M'));
    m_cellHeight = fm.height();
    m_baseline   = fm.ascent();
    if (m_fallback) {
        // QRawFont takes a pixel size; derive it from the QFont metrics so the
        // fallback glyphs render at the same visual size as the primary face.
        const qreal pixelSize = fm.ascent() + fm.descent();
        m_fallback->setPrimary(m_fontName, pixelSize);
    }
}

void GridItem::onGuifontChanged() {
    if (!m_conn) return;
    QString family = m_fontName;
    qreal size = m_fontSize;
    parseGuifont(m_conn->guifont(), family, size);
    m_fontName = family;
    m_fontSize = size;
    m_font.setFamily(family);
    m_font.setPointSizeF(size);
    recomputeMetrics();
    emit fontChanged();
    maybeResizeUi();
    update();
}

void GridItem::onFlush() {
    update();
}

void GridItem::blinkTick() {
    m_cursorOn = !m_cursorOn;
    update();
}

GridModel*      GridItem::grid() const { return m_conn ? m_conn->grid()       : nullptr; }
HighlightTable* GridItem::hl()   const { return m_conn ? m_conn->highlights() : nullptr; }
ModeInfo*       GridItem::mode() const { return m_conn ? m_conn->modeInfo()   : nullptr; }

int GridItem::colAt(qreal x) const {
    if (m_cellWidth <= 0) return 0;
    return static_cast<int>(x / m_cellWidth);
}

int GridItem::rowAt(qreal y) const {
    if (m_cellHeight <= 0) return 0;
    return static_cast<int>(y / m_cellHeight);
}

void GridItem::maybeResizeUi() {
    if (!m_conn || width() <= 0 || height() <= 0) return;
    // Only the global grid (id=1) drives nvim_ui_try_resize — sub-grids are
    // sized by nvim via win_pos/grid_resize, not by us.
    if (m_gridId != 1) return;
    const int cols = std::max(10, static_cast<int>(width()  / m_cellWidth));
    const int rows = std::max(3,  static_cast<int>(height() / m_cellHeight));
    if (auto* g = grid(); g && (g->gridCols(1) != cols || g->gridRows(1) != rows)) {
        m_conn->requestResize(cols, rows);
    }
}

void GridItem::geometryChange(const QRectF& newGeom, const QRectF& oldGeom) {
    QQuickPaintedItem::geometryChange(newGeom, oldGeom);
    maybeResizeUi();
}

void GridItem::focusInEvent(QFocusEvent* ev) {
    QQuickPaintedItem::focusInEvent(ev);
    m_cursorOn = true;
    update();
}

void GridItem::paint(QPainter* painter) {
    GridModel* g = grid();
    HighlightTable* h = hl();
    if (!g || !h) {
        painter->fillRect(boundingRect(), Qt::black);
        return;
    }

    // Optional micro-benchmark: enable with QVIM_PROFILE_PAINT=1.
    static const bool kProfilePaint = qEnvironmentVariableIntValue("QVIM_PROFILE_PAINT") != 0;
    QElapsedTimer paintTimer;
    if (kProfilePaint) paintTimer.start();

    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->setFont(m_font);

    painter->fillRect(boundingRect(), h->defaultBg());

    const int cols = g->gridCols(m_gridId);
    const int rows = g->gridRows(m_gridId);
    const QColor defaultBg = h->defaultBg();

    // Lazy cache of per-hl_id QFont. Building a QFont and calling
    // setBold/setItalic/setUnderline/setStrikeOut on every run is expensive
    // (QFontPrivate detach + re-resolve). Cache keyed by hl_id is safe because
    // HighlightTable::changed() triggers a repaint (i.e. a fresh paint() call,
    // so the cache stays consistent within a single frame).
    QHash<int, QFont> fontCache;
    fontCache.reserve(32);

    // Track last applied painter state to avoid redundant setFont/setPen calls
    // both within a row and across consecutive rows.
    int     lastHlIdFont = INT_MIN;
    int     lastHlIdPen  = INT_MIN;
    QColor  lastPenColor;

    for (int r = 0; r < rows; ++r) {
        const qreal y = r * m_cellHeight;
        const QRectF rowRect(0.0, y, m_cellWidth * cols, m_cellHeight);
        painter->save();
        painter->setClipRect(rowRect);

        int c = 0;
        while (c < cols) {
            const Cell& start = g->cell(m_gridId, r, c);
            const int runHl = start.hlId;
            int runEnd = c + 1;
            while (runEnd < cols && g->cell(m_gridId, r, runEnd).hlId == runHl) ++runEnd;

            const HlAttr a = h->resolved(runHl);
            const QRectF runRect(c * m_cellWidth, y, (runEnd - c) * m_cellWidth, m_cellHeight);
            if (a.bg != defaultBg) {
                painter->fillRect(runRect, a.bg);
            }

            QString runText;
            runText.reserve(runEnd - c);
            bool runHasNonAscii = false;
            for (int cc = c; cc < runEnd; ++cc) {
                const Cell& cell = g->cell(m_gridId, r, cc);
                // Right-half markers of double-width glyphs contribute no glyph
                // of their own; the left cell's glyph already spans both columns.
                if (cell.doubleWidth) continue;
                if (cell.text.isEmpty()) {
                    runText += QChar(' ');
                } else {
                    runText += cell.text;
                    if (!runHasNonAscii) {
                        for (QChar ch : cell.text) {
                            if (ch.unicode() >= 0x80) { runHasNonAscii = true; break; }
                        }
                    }
                }
            }

            // Only rebuild/apply font when the hl_id (and therefore the font
            // attribute set) actually changes. painter->save()/restore() per
            // row does NOT invalidate our cached state because we re-apply
            // before drawing whenever lastHlIdFont != runHl.
            if (runHl != lastHlIdFont) {
                auto it = fontCache.find(runHl);
                if (it == fontCache.end()) {
                    QFont rf = m_font;
                    rf.setBold(a.bold);
                    rf.setItalic(a.italic);
                    rf.setUnderline(a.underline);
                    rf.setStrikeOut(a.strikethrough);
                    it = fontCache.insert(runHl, rf);
                }
                painter->setFont(it.value());
                lastHlIdFont = runHl;
            }
            if (runHl != lastHlIdPen || a.fg != lastPenColor) {
                painter->setPen(a.fg);
                lastHlIdPen = runHl;
                lastPenColor = a.fg;
            }
            if (!runHasNonAscii) {
                // Fast path: pure ASCII, primary face handles everything.
                painter->drawText(QPointF(c * m_cellWidth, y + m_baseline), runText);
            } else {
                // Slow path: per-cell resolve so PUA / Nerd Font codepoints land
                // on the family that actually carries the glyph. drawText is
                // unreliable here because Qt's text engine will silently swap
                // the family on us at shape time.
                for (int cc = c; cc < runEnd; ++cc) {
                    const Cell& cell = g->cell(m_gridId, r, cc);
                    if (cell.doubleWidth) continue;
                    const QString& t = cell.text.isEmpty() ? QString(QChar(' ')) : cell.text;
                    char32_t cp = 0;
                    if (!t.isEmpty()) {
                        if (t.size() >= 2 && t.at(0).isHighSurrogate() && t.at(1).isLowSurrogate()) {
                            cp = QChar::surrogateToUcs4(t.at(0), t.at(1));
                        } else {
                            cp = t.at(0).unicode();
                        }
                    }
                    const QPointF baselinePos(cc * m_cellWidth, y + m_baseline);
                    if (cp < 0x80 || !m_fallback) {
                        painter->drawText(baselinePos, t);
                        continue;
                    }
                    const FontFallback::Resolved& res = m_fallback->resolve(cp);
                    if (res.isPrimary) {
                        painter->drawText(baselinePos, t);
                        continue;
                    }
                    const QVector<quint32> glyphs = res.raw.glyphIndexesForString(t);
                    if (glyphs.isEmpty()) {
                        painter->drawText(baselinePos, t);
                        continue;
                    }
                    QVector<QPointF> positions;
                    positions.reserve(glyphs.size());
                    qreal xCursor = 0.0;
                    const QVector<QPointF> advances = res.raw.advancesForGlyphIndexes(glyphs);
                    for (int i = 0; i < glyphs.size(); ++i) {
                        positions.append(QPointF(xCursor, 0.0));
                        if (i < advances.size()) xCursor += advances.at(i).x();
                    }
                    QGlyphRun gr;
                    gr.setRawFont(res.raw);
                    gr.setGlyphIndexes(glyphs);
                    gr.setPositions(positions);
                    painter->drawGlyphRun(baselinePos, gr);
                }
            }

            if (a.undercurl) {
                painter->setPen(a.sp);
                lastPenColor = a.sp;
                lastHlIdPen = INT_MIN; // force pen re-apply on next text run
                const qreal yy = y + m_cellHeight - 1.5;
                QPainterPath path;
                path.moveTo(runRect.left(), yy);
                for (qreal x = runRect.left(); x < runRect.right(); x += 4.0) {
                    path.quadTo(x + 1.0, yy + 2.0, x + 2.0, yy);
                    path.quadTo(x + 3.0, yy - 2.0, x + 4.0, yy);
                }
                painter->drawPath(path);
            }

            c = runEnd;
        }
        painter->restore();
        // QPainter::restore() reverts font/pen to whatever was active before
        // the matching save(). Invalidate the cached "last applied" state so
        // the next row re-applies on its first run.
        lastHlIdFont = INT_MIN;
        lastHlIdPen  = INT_MIN;
        lastPenColor = QColor();
    }

    if (kProfilePaint) {
        const qint64 ns = paintTimer.nsecsElapsed();
        qDebug("qvim paint: %lld us (%dx%d, %d hl entries cached)",
               ns / 1000, cols, rows, fontCache.size());
    }

    // Cursor — only render on the active grid (per ext_multigrid: the cursor
    // lives on exactly one grid at a time, whichever was the last
    // grid_cursor_goto target).
    const bool isActive = (g->activeGrid() == m_gridId);
    if (isActive && (m_cursorOn || !mode() || !mode()->cursorStyleEnabled())) {
        const int cr = g->cursorRowOf(m_gridId);
        const int cc = g->cursorColOf(m_gridId);
        if (cr >= 0 && cr < rows && cc >= 0 && cc < cols) {
            HlAttr a = h->resolved(mode() ? mode()->attrId() : 0);
            // Cursor colour resolution mirrors traditional vim: prefer the
            // cursor highlight's bg, but if it's invalid OR matches the editor
            // background (so the cursor would render invisibly on the canvas)
            // fall back to defaultFg. Catches colorschemes that leave the
            // `Cursor` group's bg unset or set to the editor bg.
            QColor curColor = a.bg.isValid() ? a.bg : h->defaultFg();
            if (curColor == h->defaultBg()) curColor = h->defaultFg();
            QRectF rect(cc * m_cellWidth, cr * m_cellHeight, m_cellWidth, m_cellHeight);
            const CursorShape shape = mode() ? static_cast<CursorShape>(mode()->cursorShapeInt())
                                             : CursorShape::Block;
            if (shape == CursorShape::Vertical) {
                rect.setWidth(std::max(1.0, m_cellWidth * 0.15));
            } else if (shape == CursorShape::Horizontal) {
                rect.setY(rect.bottom() - std::max(1.0, m_cellHeight * 0.15));
                rect.setHeight(std::max(1.0, m_cellHeight * 0.15));
            }
            painter->fillRect(rect, curColor);
            if (shape == CursorShape::Block && m_cursorOn) {
                const Cell& cell = g->cell(m_gridId, cr, cc);
                if (!cell.text.isEmpty()) {
                    painter->setPen(h->defaultBg());
                    painter->setFont(m_font);
                    painter->drawText(QPointF(cc * m_cellWidth, cr * m_cellHeight + m_baseline),
                                      cell.text);
                }
            }
        }
    }

    if (m_debugOverlay) {
        painter->setPen(QColor(255, 0, 0, 200));
        QFont overlayFont = m_font;
        overlayFont.setBold(true);
        painter->setFont(overlayFont);
        const QString dump = g->dumpAscii(m_gridId);
        int rr = 0;
        for (const QString& line : dump.split(QLatin1Char('\n'))) {
            painter->drawText(QPointF(0, rr * m_cellHeight + m_baseline), line);
            ++rr;
        }
    }
}

void GridItem::keyPressEvent(QKeyEvent* ev) {
    if (!m_conn) { ev->ignore(); return; }
    if (ev->modifiers().testFlag(Qt::ControlModifier) &&
        ev->modifiers().testFlag(Qt::ShiftModifier) &&
        ev->key() == Qt::Key_G) {
        setDebugOverlay(!m_debugOverlay);
        ev->accept();
        return;
    }
    const QString keys = InputHandler::keyToNvim(ev);
    if (!keys.isEmpty()) {
        m_conn->input(keys);
        ev->accept();
        return;
    }
    ev->ignore();
}

void GridItem::sendMouse(QMouseEvent* ev, QEvent::Type type) {
    if (!m_conn) return;
    const InputHandler::MouseInput m = InputHandler::mouseFor(ev, type);
    if (!m.valid) return;
    const int row = rowAt(ev->position().y());
    const int col = colAt(ev->position().x());
    m_conn->inputMouse(m.button, m.action, m.modifier, m_gridId, row, col);
}

void GridItem::mousePressEvent(QMouseEvent* ev)   { sendMouse(ev, QEvent::MouseButtonPress);  forceActiveFocus(); ev->accept(); }
void GridItem::mouseMoveEvent(QMouseEvent* ev)    { sendMouse(ev, QEvent::MouseMove);          ev->accept(); }
void GridItem::mouseReleaseEvent(QMouseEvent* ev) { sendMouse(ev, QEvent::MouseButtonRelease); ev->accept(); }

void GridItem::wheelEvent(QWheelEvent* ev) {
    if (!m_conn) { ev->ignore(); return; }
    const QString dir = InputHandler::wheelFor(ev->angleDelta().y(), ev->modifiers());
    if (dir.isEmpty()) { ev->ignore(); return; }
    const int row = rowAt(ev->position().y());
    const int col = colAt(ev->position().x());
    m_conn->inputMouse(QStringLiteral("wheel"), dir, QString(), m_gridId, row, col);
    ev->accept();
}

} // namespace qvim
