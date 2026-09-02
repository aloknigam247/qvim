#ifndef GRIDITEM_H
#define GRIDITEM_H

#include <QElapsedTimer>
#include <QFont>
#include <QFontMetricsF>
#include <QPointer>
#include <qqmlregistration.h>
#include <QQuickItem>
#include <QSGNode>
#include <QTimer>

#include "GridModel.h"
#include "HighlightTable.h"
#include "NvimConnector.h"
#include "RectNodePool.h"
#include "ScrollAnimator.h"
#include "TextNodePool.h"

namespace qvim {

class GridModel;
struct GridRuns;

class GridItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        qvim::NvimConnector *connector READ connector WRITE setConnector NOTIFY connectorChanged)
    Q_PROPERTY(int gridId READ gridId WRITE setGridId NOTIFY gridIdChanged)
    Q_PROPERTY(QString fontName READ fontName WRITE setFontName NOTIFY fontChanged)
    Q_PROPERTY(qreal fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    Q_PROPERTY(qreal cellWidth READ cellWidth NOTIFY fontChanged)
    Q_PROPERTY(qreal cellHeight READ cellHeight NOTIFY fontChanged)
    // Renamed from "baseline" to avoid a name clash with a FINAL member on
    // QQuickItem's metaobject (the Qt MOC silently ignores the override and
    // the property reads as read-only from QML — broke Shell.qml's
    // CursorItem.cellBaseline binding).
    Q_PROPERTY(qreal cellBaseline READ baseline NOTIFY fontChanged)
    Q_PROPERTY(bool debugOverlay READ debugOverlay WRITE setDebugOverlay NOTIFY debugOverlayChanged)

public:
    explicit GridItem(QQuickItem *parent = nullptr);
    ~GridItem() override;

    NvimConnector *connector() const { return m_conn; }
    void setConnector(NvimConnector *c);

    int gridId() const { return m_gridId; }
    void setGridId(int id);

    QString fontName() const { return m_fontName; }
    void setFontName(const QString &name);

    qreal fontSize() const { return m_fontSize; }
    void setFontSize(qreal pt);

    qreal cellWidth() const { return m_cellWidth; }
    qreal cellHeight() const { return m_cellHeight; }
    qreal baseline() const { return m_baseline; }

    bool debugOverlay() const { return m_debugOverlay; }
    void setDebugOverlay(bool v);

    Q_INVOKABLE int colAt(qreal x) const;
    Q_INVOKABLE int rowAt(qreal y) const;

    // Live smooth-scroll offset in pixels (the animator's current eased value),
    // 0 when no scroll is animating. Exposed so a tier-2 test can pin the real
    // production animation state rather than re-deriving it.
    Q_INVOKABLE qreal scrollAnimOffset() const { return m_scroll.offsetAt(m_animClock.elapsed()); }

    // Build the per-run QFont for a highlight attribute. Public for testing —
    // paint() uses this through a per-frame hl_id cache to avoid repeated
    // QFont detach/resolve. Pure function of m_font + attribute flags.
    QFont buildRunFont(const HlAttr &a) const;

signals:
    void connectorChanged();
    void gridIdChanged();
    void fontChanged();
    void debugOverlayChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void keyPressEvent(QKeyEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;
    void geometryChange(const QRectF &newGeom, const QRectF &oldGeom) override;

private:
    Q_SLOT void onGuifontChanged();
    Q_SLOT void onLinespaceChanged();
    Q_SLOT void onFlush();
    // Frame driver for the smooth-scroll ease: ticks update() while the
    // animator is active and stops itself once the offset has settled to 0.
    Q_SLOT void onScrollFrame();

    void recomputeMetrics();
    void maybeResizeUi();
    GridModel *grid() const;
    HighlightTable *hl() const;
    void sendMouse(QMouseEvent *ev, QEvent::Type type);

    // Renders the rounded pills and undercurls of the frame into a texture node
    // under m_decoRoot, or tears the node down when the frame has neither.
    // These are the only genuinely vector-shaped output; keeping them in
    // QPainter avoids reimplementing bezier tessellation, and both features are
    // rare enough that the common frame pays nothing.
    void updateDecorations(const GridRuns &runs, HighlightTable *h);

    // Point the smooth-scroll clip node at `band` (empty disables clipping) and
    // set the strip transform to the animator's current eased offset. Both run
    // on the render thread inside updatePaintNode.
    void ensureScrollNodes(QSGNode *root);
    void setScrollClip(const QRectF &band);
    void applyScrollOffset();

    QPointer<NvimConnector> m_conn;
    int m_gridId = 1;
    // Initial family is the OS-supplied fixed-width font (Consolas on Windows,
    // Menlo on macOS, monospace on Linux). nvim's option_set guifont overrides
    // this as soon as it arrives; the system fixed font only paints the brief
    // window before that and is the right "no qvim default" fallback.
    QString m_fontName;
    qreal m_fontSize = 14.0;
    QFont m_font;
    qreal m_cellWidth = 8.0;
    qreal m_cellHeight = 16.0;
    qreal m_baseline = 12.0;
    bool m_debugOverlay = false;
    int m_linespace = 0;

    // Scene-graph state. Owned by the returned root node (which Qt destroys),
    // so these are observing pointers only — they are dropped, never deleted,
    // when updatePaintNode is handed a null oldNode after a scene-graph
    // invalidation. All of it is touched exclusively on the render thread.
    QSGNode *m_bgRoot = nullptr;
    QSGNode *m_decoRoot = nullptr;
    QSGNode *m_decoNode = nullptr;
    QSGNode *m_textRoot = nullptr;
    QSGNode *m_lineRoot = nullptr;
    RectNodePool m_bgPool;
    RectNodePool m_linePool;
    TextNodePool m_textPool;

    // Smooth-scroll compositing. The strip (outgoing snapshot rows + the moved
    // region rows) is rendered into its own pools under a clip node (confining
    // it to the scrolled band) wrapping a transform node (the eased offset).
    // Static rows outside the band render in the pools above. Touched only on
    // the render thread, like the pools above.
    QSGNode *m_scrollClip = nullptr;
    QSGNode *m_scrollXform = nullptr;
    QSGNode *m_scrollBgRoot = nullptr;
    QSGNode *m_scrollTextRoot = nullptr;
    QSGNode *m_scrollLineRoot = nullptr;
    RectNodePool m_scrollBgPool;
    RectNodePool m_scrollLinePool;
    TextNodePool m_scrollTextPool;

    // Smooth-scroll animation state. m_scroll/m_stripSource are mutated only on
    // the GUI thread (onFlush + onScrollFrame) and read on the render thread
    // during sync while the GUI thread is blocked, so no lock is needed.
    ScrollAnimator m_scroll;
    QElapsedTimer m_animClock;
    QTimer m_animTimer;
    PendingScroll m_stripSource;
    // True when the model changed since the last content frame, so the next
    // updatePaintNode must rebuild runs/pools; false means an animation-only
    // tick that just advances the strip transform.
    bool m_contentDirty = true;
};

} // namespace qvim

#endif
