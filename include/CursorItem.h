#ifndef CURSORITEM_H
#define CURSORITEM_H

#include <utility>
#include <QElapsedTimer>
#include <QFont>
#include <QPointer>
#include <QPointF>
#include <qqmlregistration.h>
#include <QQuickItem>
#include <QRectF>
#include <QSGNode>
#include <QTimer>
#include <QVariantAnimation>

#include "CursorBlinkState.h"
#include "ModeInfo.h"
#include "NvimConnector.h"
#include "RectNodePool.h"
#include "TextNodePool.h"

namespace qvim {

class GridModel;
class HighlightTable;

// Cursor overlay item. Sits as a transparent sibling of the grid in Shell.qml,
// anchored fill, so its texture composites on top of every grid. Cursor blink
// and cursor moves invalidate only the previous + current cursor cell via
// update(QRect), so the grid item never repaints for cursor activity alone.
//
// The cursor lives on exactly one grid at a time (the last grid_cursor_goto
// target, exposed as GridModel::activeGrid()). A single CursorItem reads
// surfaceFor(activeGrid) to find the right pixel offset for sub-grids.
class CursorItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        qvim::NvimConnector *connector READ connector WRITE setConnector NOTIFY connectorChanged)
    Q_PROPERTY(qreal cellWidth READ cellWidth WRITE setCellWidth NOTIFY metricsChanged)
    Q_PROPERTY(qreal cellHeight READ cellHeight WRITE setCellHeight NOTIFY metricsChanged)
    // Renamed from "baseline" — QQuickItem has a FINAL member with that name
    // that hides the Q_PROPERTY override and makes it read-only from QML.
    Q_PROPERTY(qreal cellBaseline READ baseline WRITE setBaseline NOTIFY metricsChanged)
    Q_PROPERTY(QString fontName READ fontName WRITE setFontName NOTIFY fontChanged)
    Q_PROPERTY(qreal fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)

public:
    explicit CursorItem(QQuickItem *parent = nullptr);
    ~CursorItem() override;

    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

    NvimConnector *connector() const { return m_conn; }
    void setConnector(NvimConnector *c);

    qreal cellWidth() const { return m_cellWidth; }
    qreal cellHeight() const { return m_cellHeight; }
    qreal baseline() const { return m_baseline; }
    void setCellWidth(qreal v);
    void setCellHeight(qreal v);
    void setBaseline(qreal v);

    QString fontName() const { return m_fontName; }
    qreal fontSize() const { return m_fontSize; }
    void setFontName(const QString &v);
    void setFontSize(qreal v);

    // Pure rect math, static so unit tests can exercise it without
    // instantiating a QQuickItem (which needs a QGuiApplication).
    // row/col are absolute cell coordinates within the item's own coordinate
    // space (already offset by the active grid's surface position).
    static QRectF cursorRectFor(int row, int col, qreal cellWidth, qreal cellHeight,
                                CursorShape shape);
    // Pixel-space variant used by paint() during cursor-move animation when
    // the cursor's visible position is between cells. cellTopLeft is the
    // top-left corner in the same coord space as cursorRectFor's output.
    static QRectF cursorRectAtPixel(QPointF cellTopLeft, qreal cellWidth, qreal cellHeight,
                                    CursorShape shape);

signals:
    void connectorChanged();
    void metricsChanged();
    void fontChanged();

private slots:
    void onCursorActivity();   // GridModel::cursorChanged — restart blink + dirty old+new
    void onModeBlinkChanged(); // ModeInfo::currentChanged — update blink params + repaint
    void onBlinkTimeout();     // QTimer timeout — toggle visibility, dirty current rect
    void onFlush();            // NvimConnector::flush — content under cursor may have changed

private:
    GridModel *grid() const;
    HighlightTable *hl() const;
    ModeInfo *mode() const;

    // The cursor's VISIBLE rect in this item's coordinate space, derived
    // from m_animatedPos + current mode's shape. Returns null if no animated
    // position has been seeded yet (no cursorChanged has fired).
    QRectF currentCursorRect() const;

    // Top-left pixel of the TARGET cell (where nvim says the cursor should
    // be). Used by onCursorActivity to know where to animate toward. Returns
    // {false, {}} when the active grid is missing/hidden or the cursor is
    // out-of-bounds (a transient state during grid destruction).
    std::pair<bool, QPointF> targetCellTopLeft() const;

    void scheduleRepaint(); // update(QRect) over m_lastRect.united(currentRect)
    void rescheduleBlink();
    void rebuildFont();

    QPointer<NvimConnector> m_conn;
    qreal m_cellWidth = 8.0;
    qreal m_cellHeight = 16.0;
    qreal m_baseline = 12.0;
    QString m_fontName;
    qreal m_fontSize = 14.0;
    QFont m_font;
    CursorBlinkState m_blink;
    QElapsedTimer m_clock;
    QTimer m_blinkTimer;
    // Last rect we actually painted. Unioned with the next cursor rect on
    // each state change so update(QRect) covers both old and new cells —
    // the old one needs repaint to clear stale cursor pixels, the new one
    // to render the cursor in place.
    QRectF m_lastRect;

    // Cursor-move animation. Drives a QPointF (cell top-left in this item's
    // pixel space) between the previous and target cell over a short ease.
    // 80ms / OutExpo is the Goneovim default — starts fast, decelerates into
    // target, doesn't feel laggy for held j/k. Cursor SHAPE is read from
    // ModeInfo at paint time (the animation does not interpolate shape).
    //
    // The animation runs UNCONDITIONALLY on every same-content cursor move,
    // and is BYPASSED (snap) when the underlying cells changed in the same
    // batch (scroll/paste/etc., detected via GridModel::isDirty before the
    // GridItem flush gate clears it). Eased motion relative to text that
    // also moved looks wrong, so scroll snaps even though it goes through
    // the same cursorChanged signal.
    QVariantAnimation m_moveAnim;
    QPointF m_animatedPos;
    bool m_hasAnimatedPos = false;

    // Previous absolute cell coords (row/col on the active grid plus its
    // surface offset). Used to gate the move animation: smooth ease only on
    // 1-cell adjacent moves (|dRow|<=1 && |dCol|<=1). Anything larger (gg, G,
    // /search, %, H/M/L, ctrl-d/u) is a logical jump and should snap. -1
    // sentinel means we have not seen a cursorChanged yet.
    int m_prevRow = -1;
    int m_prevCol = -1;

    // Scene-graph state. Owned by the returned root node, so these are
    // observing pointers only and are dropped, never deleted, when
    // updatePaintNode is handed a null oldNode. Render thread only.
    QSGNode *m_blockRoot = nullptr;
    QSGNode *m_textRoot = nullptr;
    QSGNode *m_lineRoot = nullptr;
    RectNodePool m_blockPool;
    RectNodePool m_linePool;
    TextNodePool m_textPool;
};

} // namespace qvim

#endif
