#include "GridItem.h"
#include "CellMetrics.h"
#include "NvimConnector.h"
#include "GridModel.h"
#include "GridRuns.h"
#include "HighlightTable.h"
#include "InputHandler.h"

#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QHash>
#include <QKeyEvent>
#include <QRawFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
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
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_font.setPointSizeF(m_fontSize);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setHintingPreference(QFont::PreferFullHinting);
    m_fontName = m_font.family();
    recomputeMetrics();
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
        connect(m_conn, &NvimConnector::guifontChanged,   this, &GridItem::onGuifontChanged);
        connect(m_conn, &NvimConnector::linespaceChanged, this, &GridItem::onLinespaceChanged);
        connect(m_conn, &NvimConnector::flush,            this, &GridItem::onFlush);
        if (auto* g = grid()) {
            connect(g, &GridModel::sizeChanged,   this, [this]{ update(); });
        }
        if (auto* h = hl()) {
            connect(h, &HighlightTable::changed, this, [this]{ update(); });
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
    // Delegated to computeCellMetrics for testability and to keep the
    // integer-snap rationale (visual-mode seams from sub-pixel boundaries)
    // in one place.
    //
    // The cellWidth chosen by computeCellMetrics (LCM-unit snapped for
    // device-pixel alignment) usually differs from the font's natural
    // horizontalAdvance for monospace characters. Without correction
    // QPainter::drawText lays each glyph at its font-native advance, so
    // over a row of N characters the visible text drifts away from the
    // cell-boundary grid by N*(cellWidth - nativeAdvance) px. Cursor and
    // click logic both use cellWidth, so the cursor block ends up over a
    // visually different character than the buffer position it represents.
    //
    // QFont::setLetterSpacing(AbsoluteSpacing, delta) adds delta px after
    // every glyph's advance. Setting delta = cellWidth - nativeAdvance
    // forces the effective advance to equal cellWidth, so drawText layout
    // and cell math agree pixel-for-pixel.
    m_font.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    const QFontMetricsF fm(m_font);
    const qreal dpr = window() ? window()->devicePixelRatio() : 1.0;
    const CellMetrics cm = computeCellMetrics(fm, m_linespace, dpr);
    m_cellWidth  = cm.cellWidth;
    m_cellHeight = cm.cellHeight;
    m_baseline   = cm.baseline;
    const qreal nativeAdvance = fm.horizontalAdvance(QLatin1Char('M'));
    m_font.setLetterSpacing(QFont::AbsoluteSpacing, m_cellWidth - nativeAdvance);
}

void GridItem::onLinespaceChanged() {
    if (!m_conn) return;
    const int ls = m_conn->linespace();
    if (ls == m_linespace) return;
    m_linespace = ls;
    recomputeMetrics();
    emit fontChanged();
    maybeResizeUi();
    update();
}

QFont GridItem::buildRunFont(const HlAttr& a) const {
    QFont rf = m_font;
    rf.setWeight(a.bold ? QFont::Bold : QFont::Normal);
    rf.setItalic(a.italic);
    rf.setStrikeOut(a.strikethrough);
    return rf;
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
    // Skip update() when nvim's batch between flushes touched no cells on
    // this grid (the common case for held j/k that doesn't scroll past the
    // viewport: nvim emits grid_cursor_goto + flush only, no grid_line).
    // The CursorItem overlay handles its own cursor-rect repaint per flush;
    // without this guard, every keystroke at 300x100 fullscreen re-ran the
    // full rows*cols paint loop on this item.
    if (auto* g = grid(); g && g->takeDirty(m_gridId)) {
        update();
    }
}

GridModel*      GridItem::grid() const { return m_conn ? m_conn->grid()       : nullptr; }
HighlightTable* GridItem::hl()   const { return m_conn ? m_conn->highlights() : nullptr; }

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
    // Hoist underline thickness: m_font is constant across the paint, so
    // constructing QFontMetricsF per underlined run would allocate per cell
    // in the hot path. Compute once and reuse.
    const qreal underlineThickness = std::max(1.0, std::round(QFontMetricsF(m_font).lineWidth()));

    // Single traversal of the grid, resolved into draw-ready runs. Keeping the
    // walk out of the painter makes it unit-testable without a window and lets
    // the background, glyph and decoration passes share one pass over the model.
    const GridRuns runs = buildGridRuns(*g, *h, m_gridId);

    // Rounded-corner pass for cells flagged isRounded (from ext_hlstate's
    // info array on hl_attr_define, configured via g:qvim_rounded_highlights).
    // Group connected spans into closed rectilinear polygons (one per multi-row
    // selection blob) and emit a single rounded path per polygon. Every corner
    // of the polygon is rounded — convex outer corners bulge away from the
    // interior, concave inner corners bulge into it (VS Code-style inverse
    // rounding). Drawn before the run loop so glyphs paint on top; the run loop
    // skips per-cell bg fills for isRounded cells so the rounded shape isn't
    // overpainted.
    {
        const QVector<PillSpan>& spans = runs.pills;

        if (!spans.empty()) {
            const qreal radius = 5.0;

            // Backing pass. The rounded outline below deliberately cuts convex
            // corners so something other than the pill shows through — but the
            // run loop skips per-cell background fills for rounded cells, so
            // without this the only paint underneath is the whole-item
            // default-bg clear above. A pill sitting on a CursorLine (or any
            // line whose ambient background isn't the protocol default) would
            // then show default-coloured notches at every corner.
            //
            // Repaint each span's cell rect with the ambient background sampled
            // during collection. Runs before Antialiasing is enabled so these
            // axis-aligned rects rasterise hard-edged and cannot leave seams
            // against the run loop's own (also aliased) fills.
            //
            // Skipped when the span draws no pill (same guard as the group loop
            // below), and when there is nothing to inherit — a span whose
            // ambient IS the default must keep showing the default.
            for (const PillSpan& s : spans) {
                if (!s.backBg.isValid() || s.backBg == defaultBg) continue;
                const QColor pillBg = h->resolved(s.hlId).bg;
                if (!pillBg.isValid() || pillBg == defaultBg) continue;
                painter->fillRect(QRectF(s.c0 * m_cellWidth,
                                         s.row * m_cellHeight,
                                         (s.c1 - s.c0) * m_cellWidth,
                                         m_cellHeight),
                                  s.backBg);
            }

            // Group spans into connected polygons. Two spans are in the same
            // group iff they sit on consecutive rows AND their column ranges
            // overlap. Row gaps or non-overlapping pairs start a new polygon.
            QVarLengthArray<int, 16> groupStart;
            groupStart.push_back(0);
            for (int i = 1; i < spans.size(); ++i) {
                const PillSpan& pa = spans[i - 1];
                const PillSpan& pb = spans[i];
                const bool sameGroup = pb.row == pa.row + 1 &&
                                       pb.c0 < pa.c1 && pa.c0 < pb.c1 &&
                                       h->resolved(pb.hlId).bg == h->resolved(pa.hlId).bg;
                if (!sameGroup) groupStart.push_back(i);
            }
            groupStart.push_back(spans.size());

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);

            for (int gi = 0; gi + 1 < groupStart.size(); ++gi) {
                const int gs = groupStart[gi];
                const int ge = groupStart[gi + 1];
                const HlAttr a = h->resolved(spans[gs].hlId);
                if (!a.bg.isValid() || a.bg == defaultBg) continue;

                // Build the closed outline as a list of 90-degree corners in
                // clockwise order. Top edge → right side descending (with a
                // horizontal step at every c1 mismatch) → bottom edge → left
                // side ascending (with a step at every c0 mismatch).
                QVarLengthArray<QPointF, 64> verts;
                verts.push_back({spans[gs].c0 * m_cellWidth,
                                 spans[gs].row * m_cellHeight});
                verts.push_back({spans[gs].c1 * m_cellWidth,
                                 spans[gs].row * m_cellHeight});
                for (int i = gs; i < ge - 1; ++i) {
                    if (spans[i].c1 != spans[i + 1].c1) {
                        const qreal y = (spans[i].row + 1) * m_cellHeight;
                        verts.push_back({spans[i].c1     * m_cellWidth, y});
                        verts.push_back({spans[i + 1].c1 * m_cellWidth, y});
                    }
                }
                verts.push_back({spans[ge - 1].c1 * m_cellWidth,
                                 (spans[ge - 1].row + 1) * m_cellHeight});
                verts.push_back({spans[ge - 1].c0 * m_cellWidth,
                                 (spans[ge - 1].row + 1) * m_cellHeight});
                for (int i = ge - 1; i > gs; --i) {
                    if (spans[i].c0 != spans[i - 1].c0) {
                        const qreal y = spans[i].row * m_cellHeight;
                        verts.push_back({spans[i].c0     * m_cellWidth, y});
                        verts.push_back({spans[i - 1].c0 * m_cellWidth, y});
                    }
                }

                // Round every corner with a quadratic Bezier whose control
                // point IS the corner itself. The curve always bulges TOWARD
                // the control point — that direction is outside-the-polygon
                // for convex corners (smooth outer round) and inside-the-
                // polygon for concave corners (inverse round, eating a small
                // bite into the polygon at the indent). Cap rEff at half the
                // adjacent edge length so short spans don't produce arcs that
                // overlap each other.
                QPainterPath path;
                const int nv = verts.size();
                for (int i = 0; i < nv; ++i) {
                    const QPointF& prev = verts[(i + nv - 1) % nv];
                    const QPointF& curr = verts[i];
                    const QPointF& next = verts[(i + 1) % nv];
                    const QPointF dIn  = curr - prev;
                    const QPointF dOut = next - curr;
                    const qreal lenIn  = qAbs(dIn.x())  + qAbs(dIn.y());
                    const qreal lenOut = qAbs(dOut.x()) + qAbs(dOut.y());
                    const qreal rEff = std::min({radius, lenIn / 2.0, lenOut / 2.0});
                    const QPointF pIn  = curr - QPointF(dIn.x()  / lenIn,  dIn.y()  / lenIn)  * rEff;
                    const QPointF pOut = curr + QPointF(dOut.x() / lenOut, dOut.y() / lenOut) * rEff;
                    if (i == 0) path.moveTo(pIn);
                    else        path.lineTo(pIn);
                    path.quadTo(curr, pOut);
                }
                path.closeSubpath();
                painter->fillPath(path, a.bg);
            }
            painter->restore();
        }
    }

    // Lazy cache of per-hl_id QFont. Building a QFont and calling
    // setBold/setItalic/setUnderline/setStrikeOut on every run is expensive
    // (QFontPrivate detach + re-resolve). Cache keyed by hl_id is safe because
    // HighlightTable::changed() triggers a repaint (i.e. a fresh paint() call,
    // so the cache stays consistent within a single frame).
    QHash<int, QFont> fontCache;
    fontCache.reserve(32);

    // Per-hl_id QRawFont cache for the PUA overlay pass below. QRawFont::fromFont
    // is moderately expensive (~10us); caching turns the overlay's per-cluster
    // cost into a hash lookup. Same single-frame lifetime as fontCache.
    QHash<int, QRawFont> rawFontCache;
    rawFontCache.reserve(8);

    // Track last applied painter state to avoid redundant setFont/setPen calls
    // both within a row and across consecutive rows.
    int     lastHlIdFont = INT_MIN;
    int     lastHlIdPen  = INT_MIN;
    QColor  lastPenColor;

    // Walk the precomputed runs, which are already ordered row-major and
    // left-to-right. Per run we emit background, glyphs and decorations in the
    // same sequence the old inline loop did, so a later run's background still
    // overpaints an earlier run's glyph overflow.
    int runIdx = 0;
    int puaIdx = 0;
    for (int r = 0; r < rows; ++r) {
        const qreal y = r * m_cellHeight;
        const QRectF rowRect(0.0, y, m_cellWidth * cols, m_cellHeight);
        painter->save();
        painter->setClipRect(rowRect);

        for (; runIdx < runs.runs.size() && runs.runs[runIdx].row == r; ++runIdx) {
            const CellRun& run = runs.runs[runIdx];
            const QRectF runRect(run.c0 * m_cellWidth, y,
                                 (run.c1 - run.c0) * m_cellWidth, m_cellHeight);
            if (run.fillBg) painter->fillRect(runRect, run.bg);

            // Only rebuild/apply font when the hl_id (and therefore the font
            // attribute set) actually changes. painter->save()/restore() per
            // row does NOT invalidate our cached state because we re-apply
            // before drawing whenever lastHlIdFont != run.hlId.
            if (run.hlId != lastHlIdFont) {
                auto it = fontCache.find(run.hlId);
                if (it == fontCache.end())
                    it = fontCache.insert(run.hlId, buildRunFont(h->resolved(run.hlId)));
                painter->setFont(it.value());
                lastHlIdFont = run.hlId;
            }
            if (run.hlId != lastHlIdPen || run.fg != lastPenColor) {
                painter->setPen(run.fg);
                lastHlIdPen  = run.hlId;
                lastPenColor = run.fg;
            }
            painter->drawText(QPointF(run.c0 * m_cellWidth, y + m_baseline), run.text);

            if (run.underline) {
                painter->fillRect(QRectF(runRect.left(),
                                         y + m_cellHeight - underlineThickness,
                                         runRect.width(), underlineThickness),
                                  run.fg);
            }

            if (run.undercurl) {
                painter->setPen(run.sp);
                lastPenColor = run.sp;
                lastHlIdPen  = INT_MIN; // force pen re-apply on next text run
                const qreal yy = y + m_cellHeight - 1.5;
                QPainterPath path;
                path.moveTo(runRect.left(), yy);
                for (qreal x = runRect.left(); x < runRect.right(); x += 4.0) {
                    path.quadTo(x + 1.0, yy + 2.0, x + 2.0, yy);
                    path.quadTo(x + 3.0, yy - 2.0, x + 4.0, yy);
                }
                painter->drawPath(path);
            }
        }

        // PUA overlay pass. Qt 6's text shaper drops Private Use Area
        // codepoints (QTBUG-110502 closed Won't Do, QTBUG-116417 open with the
        // root cause traced to QChar::isPrint() returning false for PUA in
        // qtextengine.cpp's shaper). Result: QPainter::drawText silently emits
        // no glyph for nerd-font icons, powerline separators, etc., even when
        // the resolved font's cmap has them. The Qt-blessed workaround is to
        // bypass the shaper via QRawFont's direct cmap lookup + drawGlyphRun.
        // Rows without PUA never enter this loop and so keep the plain
        // drawText path (and with it Qt's automatic font fallback, which is
        // what makes characters like U+1F600 render via the system emoji font
        // when the primary font lacks them).
        for (; puaIdx < runs.puaClusters.size() && runs.puaClusters[puaIdx].row == r; ++puaIdx) {
            const PuaCluster& cluster = runs.puaClusters[puaIdx];
            const HlAttr ha = h->resolved(cluster.hlId);

            auto fit = fontCache.find(cluster.hlId);
            if (fit == fontCache.end()) fit = fontCache.insert(cluster.hlId, buildRunFont(ha));
            auto rit = rawFontCache.find(cluster.hlId);
            if (rit == rawFontCache.end())
                rit = rawFontCache.insert(cluster.hlId, QRawFont::fromFont(fit.value()));

            QList<quint32> glyphs;
            QList<QPointF> positions;
            glyphs.reserve(cluster.c1 - cluster.c0);
            positions.reserve(cluster.c1 - cluster.c0);
            for (int i = cluster.c0; i < cluster.c1; ++i) {
                const Cell& pcell = g->cell(m_gridId, r, i);
                if (pcell.doubleWidth) continue;
                const QList<quint32> ids = rit.value().glyphIndexesForString(pcell.text);
                if (!ids.isEmpty()) {
                    glyphs.append(ids.first());
                    positions.append(QPointF(i * m_cellWidth, y + m_baseline));
                }
            }

            if (!glyphs.isEmpty()) {
                painter->setPen(ha.fg);
                QGlyphRun run;
                run.setRawFont(rit.value());
                run.setGlyphIndexes(glyphs);
                run.setPositions(positions);
                painter->drawGlyphRun(QPointF(0, 0), run);
                // Pen color may have changed; force next run's pen re-apply.
                lastHlIdPen  = INT_MIN;
                lastPenColor = QColor();
            }
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

    // Cursor is rendered by CursorItem, a sibling overlay in Shell.qml.
    // Splitting it out of GridItem::paint() avoids re-running this entire
    // row×col loop on every blink half-cycle and every cursor move.

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
    const auto w = InputHandler::wheelFor(ev->angleDelta().x(), ev->angleDelta().y(),
                                          ev->modifiers());
    if (!w.valid) { ev->ignore(); return; }
    const int row = rowAt(ev->position().y());
    const int col = colAt(ev->position().x());
    m_conn->inputMouse(w.button, w.action, w.modifier, m_gridId, row, col);
    ev->accept();
}

} // namespace qvim
