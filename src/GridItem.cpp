#include "GridItem.h"
#include "CellMetrics.h"
#include "GridModel.h"
#include "GridRuns.h"
#include "HighlightTable.h"
#include "InputHandler.h"
#include "NvimConnector.h"
#include "StripComposition.h"

#include <climits>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QHash>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QRawFont>
#include <QRegularExpression>
#include <QSGClipNode>
#include <QSGGeometry>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGTransformNode>
#include <QtGlobal>
#include <QtMath>
#include <QVarLengthArray>
#include <QVector>
#include <QWheelEvent>

namespace qvim {

static void parseGuifont(const QString &guifont, QString &family, qreal &size) {
    if(guifont.isEmpty()) return;
    const auto parts = guifont.split(QLatin1Char(':'));
    if(parts.isEmpty()) return;
    family = parts.first();
    family.replace(QLatin1Char('_'), QLatin1Char(' '));
    for(int i = 1; i < parts.size(); ++i) {
        const QString &p = parts.at(i);
        if(p.startsWith(QLatin1Char('h')) && p.size() > 1) {
            bool ok = false;
            const qreal v = p.mid(1).toDouble(&ok);
            if(ok) size = v;
        }
    }
}

GridItem::~GridItem() = default;

GridItem::GridItem(QQuickItem *parent) : QQuickItem(parent) {
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

    // Drives the eased offset between content flushes. ~60 Hz is enough for an
    // 80 ms ease; each tick only advances one matrix (see onScrollFrame).
    m_animClock.start();
    m_animTimer.setInterval(16);
    connect(&m_animTimer, &QTimer::timeout, this, &GridItem::onScrollFrame);
}

void GridItem::setGridId(int id) {
    if(m_gridId == id) return;
    m_gridId = id;
    emit gridIdChanged();
    update();
}

void GridItem::setConnector(NvimConnector *c) {
    if(m_conn == c) return;
    if(m_conn) disconnect(m_conn, nullptr, this, nullptr);
    m_conn = c;
    if(m_conn) {
        connect(m_conn, &NvimConnector::guifontChanged, this, &GridItem::onGuifontChanged);
        connect(m_conn, &NvimConnector::linespaceChanged, this, &GridItem::onLinespaceChanged);
        connect(m_conn, &NvimConnector::flush, this, &GridItem::onFlush);
        if(auto *g = grid()) {
            connect(g, &GridModel::sizeChanged, this, [this] { update(); });
        }
        if(auto *h = hl()) {
            connect(h, &HighlightTable::changed, this, [this] { update(); });
        }
    }
    emit connectorChanged();
    update();
}

void GridItem::setFontName(const QString &name) {
    if(name == m_fontName) return;
    m_fontName = name;
    m_font.setFamily(name);
    recomputeMetrics();
    emit fontChanged();
    maybeResizeUi();
    update();
}

void GridItem::setFontSize(qreal pt) {
    if(qFuzzyCompare(pt, m_fontSize)) return;
    m_fontSize = pt;
    m_font.setPointSizeF(pt);
    recomputeMetrics();
    emit fontChanged();
    maybeResizeUi();
    update();
}

void GridItem::setDebugOverlay(bool v) {
    if(v == m_debugOverlay) return;
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
    m_cellWidth = cm.cellWidth;
    m_cellHeight = cm.cellHeight;
    m_baseline = cm.baseline;
    const qreal nativeAdvance = fm.horizontalAdvance(QLatin1Char('M'));
    m_font.setLetterSpacing(QFont::AbsoluteSpacing, m_cellWidth - nativeAdvance);
}

void GridItem::onLinespaceChanged() {
    if(!m_conn) return;
    const int ls = m_conn->linespace();
    if(ls == m_linespace) return;
    m_linespace = ls;
    recomputeMetrics();
    emit fontChanged();
    maybeResizeUi();
    update();
}

QFont GridItem::buildRunFont(const HlAttr &a) const {
    QFont rf = m_font;
    rf.setWeight(a.bold ? QFont::Bold : QFont::Normal);
    rf.setItalic(a.italic);
    rf.setStrikeOut(a.strikethrough);
    return rf;
}

void GridItem::onGuifontChanged() {
    if(!m_conn) return;
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
    auto *g = grid();
    if(!g) return;

    const bool dirty = g->takeDirty(m_gridId);
    PendingScroll ps = g->takeScroll(m_gridId);
    const qint64 now = m_animClock.elapsed();

    if(ps.valid && ps.animatable && m_gridId == 1 && m_cellHeight > 0.0) {
        // Snap-then-restart: reseed from this step's outgoing rows. Held j/k
        // shows a rapid series of short eases; the snapshot backing the current
        // animation is always the current step's rows.
        m_stripSource = std::move(ps);
        m_scroll.start(m_stripSource.delta * m_cellHeight, now);
        m_contentDirty = true;
        if(!m_animTimer.isActive()) m_animTimer.start();
        update();
        return;
    }

    // A scroll that isn't animatable (partial width, top!=0, whole-region jump,
    // sub-grid, or a second scroll collapsed into the batch), or an unrelated
    // content change mid-ease (paste / broad redraw), must not keep easing
    // against rows that just moved — snap straight to the final image.
    if(ps.valid || (dirty && m_scroll.active(now))) {
        m_scroll.snap();
        m_animTimer.stop();
    }

    if(dirty) {
        m_contentDirty = true;
        update();
    }
}

void GridItem::onScrollFrame() {
    // Advance the ease. active() goes false once the duration elapses; the
    // final tick still runs update() so the offset settles to exactly 0.
    if(!m_scroll.active(m_animClock.elapsed())) m_animTimer.stop();
    update();
}

GridModel *GridItem::grid() const { return m_conn ? m_conn->grid() : nullptr; }
HighlightTable *GridItem::hl() const { return m_conn ? m_conn->highlights() : nullptr; }

int GridItem::colAt(qreal x) const {
    if(m_cellWidth <= 0) return 0;
    return static_cast<int>(x / m_cellWidth);
}

int GridItem::rowAt(qreal y) const {
    if(m_cellHeight <= 0) return 0;
    return static_cast<int>(y / m_cellHeight);
}

void GridItem::maybeResizeUi() {
    if(!m_conn || width() <= 0 || height() <= 0) return;
    if(m_gridId != 1) return;
    const int cols = std::max(10, static_cast<int>(width() / m_cellWidth));
    const int rows = std::max(3, static_cast<int>(height() / m_cellHeight));
    if(auto *g = grid(); g && (g->gridCols(1) != cols || g->gridRows(1) != rows)) {
        m_conn->requestResize(cols, rows);
    }
}

void GridItem::geometryChange(const QRectF &newGeom, const QRectF &oldGeom) {
    QQuickItem::geometryChange(newGeom, oldGeom);
    maybeResizeUi();
}

void GridItem::updateDecorations(const GridRuns &runs, HighlightTable *h) {
    const QColor defaultBg = h->defaultBg();
    const QVector<PillSpan> &spans = runs.pills;

    // Union bounding box of everything this layer draws. Both features are rare
    // (pills need g:qvim_rounded_highlights, undercurl needs diagnostics), so
    // the common frame finds nothing and skips the texture entirely.
    QRectF bbox;
    for(const PillSpan &s: spans) {
        const QColor pillBg = h->resolved(s.hlId).bg;
        if(!pillBg.isValid() || pillBg == defaultBg) continue;
        bbox = bbox.united(QRectF(s.c0 * m_cellWidth, s.row * m_cellHeight,
                                  (s.c1 - s.c0) * m_cellWidth, m_cellHeight));
    }
    for(const CellRun &run: runs.runs) {
        if(!run.undercurl) continue;
        bbox =
            bbox.united(QRectF(run.c0 * m_cellWidth, (run.row * m_cellHeight) + m_cellHeight - 4.0,
                               (run.c1 - run.c0) * m_cellWidth, 4.0));
    }

    if(bbox.isEmpty()) {
        if(m_decoNode) {
            m_decoRoot->removeChildNode(m_decoNode);
            delete m_decoNode;
            m_decoNode = nullptr;
        }
        return;
    }

    bbox = bbox.intersected(boundingRect());
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    QImage img(QSize(qCeil(bbox.width() * dpr), qCeil(bbox.height() * dpr)),
               QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);

    {
        QPainter p(&img);
        p.translate(-bbox.topLeft());
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);

        if(!spans.empty()) {
            const qreal radius = 5.0;

            // Group spans into connected polygons. Two spans are in the same
            // group iff they sit on consecutive rows AND their column ranges
            // overlap. Row gaps or non-overlapping pairs start a new polygon.
            QVarLengthArray<int, 16> groupStart;
            groupStart.push_back(0);
            for(int i = 1; i < spans.size(); ++i) {
                const PillSpan &pa = spans[i - 1];
                const PillSpan &pb = spans[i];
                const bool sameGroup = pb.row == pa.row + 1 && pb.c0 < pa.c1 && pa.c0 < pb.c1 &&
                                       h->resolved(pb.hlId).bg == h->resolved(pa.hlId).bg;
                if(!sameGroup) groupStart.push_back(i);
            }
            groupStart.push_back(static_cast<int>(spans.size()));

            for(int gi = 0; gi + 1 < groupStart.size(); ++gi) {
                const int gs = groupStart[gi];
                const int ge = groupStart[gi + 1];
                const HlAttr a = h->resolved(spans[gs].hlId);
                if(!a.bg.isValid() || a.bg == defaultBg) continue;

                // Build the closed outline as a list of 90-degree corners in
                // clockwise order. Top edge -> right side descending (with a
                // horizontal step at every c1 mismatch) -> bottom edge -> left
                // side ascending (with a step at every c0 mismatch).
                QVarLengthArray<QPointF, 64> verts;
                verts.push_back({ spans[gs].c0 * m_cellWidth, spans[gs].row * m_cellHeight });
                verts.push_back({ spans[gs].c1 * m_cellWidth, spans[gs].row * m_cellHeight });
                for(int i = gs; i < ge - 1; ++i) {
                    if(spans[i].c1 != spans[i + 1].c1) {
                        const qreal y = (spans[i].row + 1) * m_cellHeight;
                        verts.push_back({ spans[i].c1 * m_cellWidth, y });
                        verts.push_back({ spans[i + 1].c1 * m_cellWidth, y });
                    }
                }
                verts.push_back(
                    { spans[ge - 1].c1 * m_cellWidth, (spans[ge - 1].row + 1) * m_cellHeight });
                verts.push_back(
                    { spans[ge - 1].c0 * m_cellWidth, (spans[ge - 1].row + 1) * m_cellHeight });
                for(int i = ge - 1; i > gs; --i) {
                    if(spans[i].c0 != spans[i - 1].c0) {
                        const qreal y = spans[i].row * m_cellHeight;
                        verts.push_back({ spans[i].c0 * m_cellWidth, y });
                        verts.push_back({ spans[i - 1].c0 * m_cellWidth, y });
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
                const int nv = static_cast<int>(verts.size());
                for(int i = 0; i < nv; ++i) {
                    const QPointF &prev = verts[(i + nv - 1) % nv];
                    const QPointF &curr = verts[i];
                    const QPointF &next = verts[(i + 1) % nv];
                    const QPointF dIn = curr - prev;
                    const QPointF dOut = next - curr;
                    const qreal lenIn = qAbs(dIn.x()) + qAbs(dIn.y());
                    const qreal lenOut = qAbs(dOut.x()) + qAbs(dOut.y());
                    const qreal rEff = std::min({ radius, lenIn / 2.0, lenOut / 2.0 });
                    const QPointF pIn = curr - QPointF(dIn.x() / lenIn, dIn.y() / lenIn) * rEff;
                    const QPointF pOut =
                        curr + QPointF(dOut.x() / lenOut, dOut.y() / lenOut) * rEff;
                    if(i == 0) path.moveTo(pIn);
                    else path.lineTo(pIn);
                    path.quadTo(curr, pOut);
                }
                path.closeSubpath();
                p.fillPath(path, a.bg);
            }
        }

        for(const CellRun &run: runs.runs) {
            if(!run.undercurl) continue;
            const qreal left = run.c0 * m_cellWidth;
            const qreal right = run.c1 * m_cellWidth;
            const qreal yy = (run.row * m_cellHeight) + m_cellHeight - 1.5;
            QPainterPath path;
            path.moveTo(left, yy);
            for(int i = 0;; ++i) {
                const qreal x = left + (i * 4.0);
                if(x >= right) break;
                path.quadTo(x + 1.0, yy + 2.0, x + 2.0, yy);
                path.quadTo(x + 3.0, yy - 2.0, x + 4.0, yy);
            }
            p.setPen(run.sp);
            p.strokePath(path, QPen(run.sp));
        }
    }

    auto *tex = window()->createTextureFromImage(img, QQuickWindow::TextureHasAlphaChannel);
    // QSGNode is not polymorphic (it dispatches on type() rather than a vtable), so dynamic_cast
    // is unavailable; m_decoNode is one we created as a QSGSimpleTextureNode.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    auto *node = static_cast<QSGSimpleTextureNode *>(m_decoNode);
    if(!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Nearest);
        m_decoRoot->appendChildNode(node);
        m_decoNode = node;
    }
    node->setTexture(tex);
    node->setRect(bbox);
    node->markDirty(QSGNode::DirtyMaterial);
}

// Emit one block of already-resolved runs into the given pools. `yMap(row)`
// gives a run's pixel top and `cellAt(row, col)` resolves a PUA cell's glyph,
// so the same routine serves the normal grid (natural y, live cells) and the
// smooth-scroll strip (placement y, snapshot cells). Only rows in [rowLo, rowHi)
// are drawn. Submission order (pill backing, then run fills + glyphs, then
// underlines) matches the pre-refactor single pass.
template <class YMap, class CellAt>
static void renderRuns(const GridRuns &runs, const HighlightTable &h, const GridItem &item,
                       RectNodePool &bgPool, TextNodePool &textPool, RectNodePool &linePool,
                       QHash<int, QFont> &fontCache, qreal cellWidth, qreal cellHeight,
                       qreal baseline, qreal underlineThickness, const QColor &defaultBg, int rowLo,
                       int rowHi, YMap yMap, CellAt cellAt) {
    // Backing fill for rounded spans; see the note in the previous single-pass
    // renderer. Kept first so run background fills paint over it.
    for(const PillSpan &s: runs.pills) {
        if(s.row < rowLo || s.row >= rowHi) continue;
        if(!s.backBg.isValid() || s.backBg == defaultBg) continue;
        const QColor pillBg = h.resolved(s.hlId).bg;
        if(!pillBg.isValid() || pillBg == defaultBg) continue;
        bgPool.add(QRectF(s.c0 * cellWidth, yMap(s.row), (s.c1 - s.c0) * cellWidth, cellHeight),
                   s.backBg);
    }

    for(const CellRun &run: runs.runs) {
        if(run.row < rowLo || run.row >= rowHi) continue;
        const qreal y = yMap(run.row);
        const QRectF runRect(run.c0 * cellWidth, y, (run.c1 - run.c0) * cellWidth, cellHeight);
        if(run.fillBg) bgPool.add(runRect, run.bg);

        auto it = fontCache.find(run.hlId);
        if(it == fontCache.end())
            it = fontCache.insert(run.hlId, item.buildRunFont(h.resolved(run.hlId)));

        textPool.addText(run.hlId, run.text, it.value(), run.fg,
                         QPointF(run.c0 * cellWidth, y + baseline));

        if(run.underline) {
            linePool.add(QRectF(runRect.left(), y + cellHeight - underlineThickness,
                                runRect.width(), underlineThickness),
                         run.fg);
        }
    }

    // PUA pass. Qt's shaper gives Private Use Area codepoints a zero advance
    // (QTBUG-116417), so a run containing several of them collapses into one
    // spot. Laying out each PUA cell separately at its own x sidesteps the
    // broken advance entirely — the grid supplies every position itself, so the
    // shaper is never asked to place one PUA glyph relative to another.
    for(const PuaCluster &cluster: runs.puaClusters) {
        if(cluster.row < rowLo || cluster.row >= rowHi) continue;
        auto it = fontCache.find(cluster.hlId);
        if(it == fontCache.end())
            it = fontCache.insert(cluster.hlId, item.buildRunFont(h.resolved(cluster.hlId)));
        const QColor fg = h.resolved(cluster.hlId).fg;
        const qreal y = yMap(cluster.row);
        for(int c = cluster.c0; c < cluster.c1; ++c) {
            const Cell &pcell = cellAt(cluster.row, c);
            if(pcell.doubleWidth || pcell.text.isEmpty()) continue;
            textPool.addText(cluster.hlId, pcell.text, it.value(), fg,
                             QPointF(c * cellWidth, y + baseline));
        }
    }
}

void GridItem::ensureScrollNodes(QSGNode *root) {
    if(m_scrollClip) return;

    // clip (confines the strip to the scrolled band) -> transform (eased
    // offset) -> bg/text/line roots. Appended last so the strip paints above
    // the static rows, which never overlap it (the band excludes them).
    auto *clip = new QSGClipNode;
    clip->setIsRectangular(true);
    auto *geo = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
    QSGGeometry::updateRectGeometry(geo, boundingRect());
    clip->setGeometry(geo);
    clip->setFlag(QSGNode::OwnsGeometry, true);
    clip->setClipRect(boundingRect());

    auto *xform = new QSGTransformNode;
    m_scrollBgRoot = new QSGNode;
    m_scrollTextRoot = new QSGNode;
    m_scrollLineRoot = new QSGNode;
    xform->appendChildNode(m_scrollBgRoot);
    xform->appendChildNode(m_scrollTextRoot);
    xform->appendChildNode(m_scrollLineRoot);
    clip->appendChildNode(xform);
    root->appendChildNode(clip);

    m_scrollClip = clip;
    m_scrollXform = xform;
}

void GridItem::setScrollClip(const QRectF &band) {
    if(!m_scrollClip) return;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    auto *clip = static_cast<QSGClipNode *>(m_scrollClip);
    const QRectF rect = band.isEmpty() ? boundingRect() : band;
    clip->setClipRect(rect);
    QSGGeometry::updateRectGeometry(clip->geometry(), rect);
    clip->markDirty(QSGNode::DirtyGeometry);
}

void GridItem::applyScrollOffset() {
    if(!m_scrollXform) return;
    QMatrix4x4 m;
    m.translate(0.0F, static_cast<float>(m_scroll.offsetAt(m_animClock.elapsed())));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    static_cast<QSGTransformNode *>(m_scrollXform)->setMatrix(m);
}

QSGNode *GridItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData * /*data*/) {
    // A null oldNode means the scene graph threw the previous tree away, so the
    // cached slot pointers are dangling. Drop them without deleting.
    if(!oldNode) {
        m_bgRoot = m_decoRoot = m_decoNode = m_textRoot = m_lineRoot = nullptr;
        m_scrollClip = m_scrollXform = m_scrollBgRoot = m_scrollTextRoot = m_scrollLineRoot =
            nullptr;
        m_bgPool.forget();
        m_linePool.forget();
        m_textPool.forget();
        m_scrollBgPool.forget();
        m_scrollLinePool.forget();
        m_scrollTextPool.forget();
        // The rebuilt tree starts with empty pools; a bare offset tick would
        // leave the grid blank, so force a full content frame.
        m_contentDirty = true;
    }

    QSGNode *root = oldNode;
    if(!root) {
        root = new QSGNode;
        // Four fixed slots, created once and never reordered, so z-ordering
        // cannot drift as nodes come and go: backgrounds, vector decorations,
        // text, then underlines on top.
        m_bgRoot = new QSGNode;
        m_decoRoot = new QSGNode;
        m_textRoot = new QSGNode;
        m_lineRoot = new QSGNode;
        root->appendChildNode(m_bgRoot);
        root->appendChildNode(m_decoRoot);
        root->appendChildNode(m_textRoot);
        root->appendChildNode(m_lineRoot);
    }
    ensureScrollNodes(root);

    // Animation-only tick: no model change since the last content frame and an
    // ease is in flight, so reuse every pooled node and just advance the strip
    // transform. This is the perf gate — held j/k does not turn into a
    // full-grid rebuild per animation frame.
    const qint64 nowMs = m_animClock.elapsed();
    if(!m_contentDirty && m_scroll.active(nowMs)) {
        applyScrollOffset();
        return root;
    }
    m_contentDirty = false;

    const GridModel *g = grid();
    HighlightTable *h = hl();

    m_bgPool.beginFrame(m_bgRoot, window());
    m_linePool.beginFrame(m_lineRoot, window());
    m_textPool.beginFrame(m_textRoot, window());
    m_scrollBgPool.beginFrame(m_scrollBgRoot, window());
    m_scrollLinePool.beginFrame(m_scrollLineRoot, window());
    m_scrollTextPool.beginFrame(m_scrollTextRoot, window());

    if(!g || !h) {
        m_bgPool.add(boundingRect(), Qt::black);
        m_bgPool.endFrame();
        m_linePool.endFrame();
        m_textPool.endFrame();
        m_scrollBgPool.endFrame();
        m_scrollLinePool.endFrame();
        m_scrollTextPool.endFrame();
        setScrollClip(QRectF());
        applyScrollOffset();
        if(m_decoNode) {
            m_decoRoot->removeChildNode(m_decoNode);
            delete m_decoNode;
            m_decoNode = nullptr;
        }
        return root;
    }

    // Optional micro-benchmark: enable with QVIM_PROFILE_PAINT=1.
    static const bool kProfilePaint = qEnvironmentVariableIntValue("QVIM_PROFILE_PAINT") != 0;
    QElapsedTimer paintTimer;
    if(kProfilePaint) paintTimer.start();

    const int cols = g->gridCols(m_gridId);
    const int rows = g->gridRows(m_gridId);
    const QColor defaultBg = h->defaultBg();
    // Hoist underline thickness: m_font is constant across the frame, so
    // constructing QFontMetricsF per underlined run would allocate per cell
    // in the hot path. Compute once and reuse.
    const qreal underlineThickness = std::max(1.0, std::round(QFontMetricsF(m_font).lineWidth()));

    // Single traversal of the grid, resolved into draw-ready runs.
    const GridRuns runs = buildGridRuns(*g, *h, m_gridId);

    // The default-background clear sits behind everything; the strip paints its
    // own per-run fills over it inside the band.
    m_bgPool.add(boundingRect(), defaultBg);

    // Lazy cache of per-hl_id QFont. Building a QFont and calling
    // setBold/setItalic/setStrikeOut on every run is expensive (QFontPrivate
    // detach + re-resolve). Cache keyed by hl_id is safe because
    // HighlightTable::changed() triggers a fresh frame. Shared across the
    // static and strip passes — same fonts either way.
    QHash<int, QFont> fontCache;
    fontCache.reserve(32);

    const auto natY = [this](int r) { return r * m_cellHeight; };
    const auto modelCell = [g, this](int r, int c) -> const Cell & {
        return g->cell(m_gridId, r, c);
    };

    const bool composite =
        m_scroll.active(nowMs) && m_stripSource.valid && m_stripSource.animatable;
    if(composite) {
        const int top = m_stripSource.top;
        const int bot = m_stripSource.bot;
        const int delta = m_stripSource.delta;

        // Static rows below the scrolled band (statusline / cmdline) render at
        // their natural position in the untransformed pools.
        renderRuns(runs, *h, *this, m_bgPool, m_textPool, m_linePool, fontCache, m_cellWidth,
                   m_cellHeight, m_baseline, underlineThickness, defaultBg, bot, rows, natY,
                   modelCell);

        // Moved region rows [top, bot) into the strip at natural y; the eased
        // offset is applied by the transform node, not baked into y.
        renderRuns(runs, *h, *this, m_scrollBgPool, m_scrollTextPool, m_scrollLinePool, fontCache,
                   m_cellWidth, m_cellHeight, m_baseline, underlineThickness, defaultBg, top, bot,
                   natY, modelCell);

        // Outgoing snapshot rows at their strip placement (the rows that left
        // the band; the model no longer holds them).
        const GridRuns lostRuns = buildRowsRuns(m_stripSource.lost, *h);
        const int n = static_cast<int>(m_stripSource.lost.size());
        const QVector<StripRow> placements = stripRowPlacements(delta, top, bot, m_cellHeight);
        QHash<int, qreal> lostY;
        lostY.reserve(n);
        for(const StripRow &p: placements)
            if(p.fromLost) lostY.insert(p.index, p.baseY);
        const auto lostYMap = [&lostY](int k) { return lostY.value(k); };
        const auto lostCell = [this](int r, int c) -> const Cell & {
            return m_stripSource.lost[r][c];
        };
        renderRuns(lostRuns, *h, *this, m_scrollBgPool, m_scrollTextPool, m_scrollLinePool,
                   fontCache, m_cellWidth, m_cellHeight, m_baseline, underlineThickness, defaultBg,
                   0, n, lostYMap, lostCell);

        setScrollClip(QRectF(0, top * m_cellHeight, width(), (bot - top) * m_cellHeight));

        // Decorations (rounded pills / undercurls) are skipped for the ~80 ms
        // ease: they are rare and a static texture at final positions under the
        // sliding strip would look wrong. They return on the next content frame.
        if(m_decoNode) {
            m_decoRoot->removeChildNode(m_decoNode);
            delete m_decoNode;
            m_decoNode = nullptr;
        }
    } else {
        renderRuns(runs, *h, *this, m_bgPool, m_textPool, m_linePool, fontCache, m_cellWidth,
                   m_cellHeight, m_baseline, underlineThickness, defaultBg, 0, rows, natY,
                   modelCell);

        if(m_debugOverlay) {
            QFont overlayFont = m_font;
            overlayFont.setBold(true);
            const QString dump = g->dumpAscii(m_gridId);
            int rr = 0;
            for(const QString &line: dump.split(QLatin1Char('\n'))) {
                m_textPool.addText(INT_MIN, line, overlayFont, QColor(255, 0, 0, 200),
                                   QPointF(0, (rr * m_cellHeight) + m_baseline));
                ++rr;
            }
        }

        setScrollClip(QRectF());
        updateDecorations(runs, h);
    }

    applyScrollOffset();

    m_bgPool.endFrame();
    m_linePool.endFrame();
    m_textPool.endFrame();
    m_scrollBgPool.endFrame();
    m_scrollLinePool.endFrame();
    m_scrollTextPool.endFrame();

    if(kProfilePaint) {
        const qint64 ns = paintTimer.nsecsElapsed();
        qDebug("qvim paint: %lld us (%dx%d, %d hl entries cached)", ns / 1000, cols, rows,
               fontCache.size());
    }

    // Cursor is rendered by CursorItem, a sibling overlay in Shell.qml.
    return root;
}

void GridItem::keyPressEvent(QKeyEvent *ev) {
    if(!m_conn) {
        ev->ignore();
        return;
    }
    if(ev->modifiers().testFlag(Qt::ControlModifier) &&
       ev->modifiers().testFlag(Qt::ShiftModifier) && ev->key() == Qt::Key_G) {
        setDebugOverlay(!m_debugOverlay);
        ev->accept();
        return;
    }
    const QString keys = InputHandler::keyToNvim(ev);
    if(!keys.isEmpty()) {
        m_conn->input(keys);
        ev->accept();
        return;
    }
    ev->ignore();
}

void GridItem::sendMouse(QMouseEvent *ev, QEvent::Type type) {
    if(!m_conn) return;
    const InputHandler::MouseInput m = InputHandler::mouseFor(ev, type);
    if(!m.valid) return;
    const int row = rowAt(ev->position().y());
    const int col = colAt(ev->position().x());
    m_conn->inputMouse(m.button, m.action, m.modifier, m_gridId, row, col);
}

void GridItem::mousePressEvent(QMouseEvent *ev) {
    sendMouse(ev, QEvent::MouseButtonPress);
    forceActiveFocus();
    ev->accept();
}
void GridItem::mouseMoveEvent(QMouseEvent *ev) {
    sendMouse(ev, QEvent::MouseMove);
    ev->accept();
}
void GridItem::mouseReleaseEvent(QMouseEvent *ev) {
    sendMouse(ev, QEvent::MouseButtonRelease);
    ev->accept();
}

void GridItem::wheelEvent(QWheelEvent *ev) {
    if(!m_conn) {
        ev->ignore();
        return;
    }
    const auto w =
        InputHandler::wheelFor(ev->angleDelta().x(), ev->angleDelta().y(), ev->modifiers());
    if(!w.valid) {
        ev->ignore();
        return;
    }
    const int row = rowAt(ev->position().y());
    const int col = colAt(ev->position().x());
    m_conn->inputMouse(w.button, w.action, w.modifier, m_gridId, row, col);
    ev->accept();
}

} // namespace qvim
