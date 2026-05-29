#pragma once

#include <QElapsedTimer>
#include <QFont>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QTimer>
#include <qqmlregistration.h>

#include "CursorBlinkState.h"
#include "ModeInfo.h"
#include "NvimConnector.h"

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
class CursorItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qvim::NvimConnector* connector READ connector WRITE setConnector NOTIFY connectorChanged)
    Q_PROPERTY(qreal   cellWidth    READ cellWidth    WRITE setCellWidth    NOTIFY metricsChanged)
    Q_PROPERTY(qreal   cellHeight   READ cellHeight   WRITE setCellHeight   NOTIFY metricsChanged)
    // Renamed from "baseline" — QQuickItem has a FINAL member with that name
    // that hides the Q_PROPERTY override and makes it read-only from QML.
    Q_PROPERTY(qreal   cellBaseline READ baseline     WRITE setBaseline     NOTIFY metricsChanged)
    Q_PROPERTY(QString fontName   READ fontName   WRITE setFontName   NOTIFY fontChanged)
    Q_PROPERTY(qreal   fontSize   READ fontSize   WRITE setFontSize   NOTIFY fontChanged)

public:
    explicit CursorItem(QQuickItem* parent = nullptr);
    ~CursorItem() override;

    void paint(QPainter* painter) override;

    NvimConnector* connector() const { return m_conn; }
    void setConnector(NvimConnector* c);

    qreal cellWidth()  const { return m_cellWidth; }
    qreal cellHeight() const { return m_cellHeight; }
    qreal baseline()   const { return m_baseline; }
    void  setCellWidth(qreal v);
    void  setCellHeight(qreal v);
    void  setBaseline(qreal v);

    QString fontName() const { return m_fontName; }
    qreal   fontSize() const { return m_fontSize; }
    void    setFontName(const QString& v);
    void    setFontSize(qreal v);

    // Pure rect math, static so unit tests can exercise it without
    // instantiating a QQuickPaintedItem (which needs a QGuiApplication).
    // row/col are absolute cell coordinates within the item's own coordinate
    // space (already offset by the active grid's surface position).
    static QRectF cursorRectFor(int row, int col,
                                qreal cellWidth, qreal cellHeight,
                                CursorShape shape);

signals:
    void connectorChanged();
    void metricsChanged();
    void fontChanged();

private slots:
    void onCursorActivity();    // GridModel::cursorChanged — restart blink + dirty old+new
    void onModeBlinkChanged();  // ModeInfo::currentChanged — update blink params + repaint
    void onBlinkTimeout();      // QTimer timeout — toggle visibility, dirty current rect
    void onFlush();             // NvimConnector::flush — content under cursor may have changed

private:
    GridModel*      grid() const;
    HighlightTable* hl()   const;
    ModeInfo*       mode() const;

    // Current cursor rect in this item's coordinate space, or null if there
    // is no active grid / cursor is off-grid. Reads activeGrid/cursor position
    // off the model — no caching, called once per paint and per state change.
    QRectF currentCursorRect() const;

    void scheduleRepaint();   // update(QRect) over m_lastRect.united(currentRect)
    void rescheduleBlink();
    void rebuildFont();

    QPointer<NvimConnector> m_conn;
    qreal     m_cellWidth   = 8.0;
    qreal     m_cellHeight  = 16.0;
    qreal     m_baseline    = 12.0;
    QString   m_fontName;
    qreal     m_fontSize    = 14.0;
    QFont     m_font;
    CursorBlinkState m_blink;
    QElapsedTimer    m_clock;
    QTimer           m_blinkTimer;
    // Last rect we actually painted. Unioned with the next cursor rect on
    // each state change so update(QRect) covers both old and new cells —
    // the old one needs repaint to clear stale cursor pixels, the new one
    // to render the cursor in place.
    QRectF           m_lastRect;
};

} // namespace qvim
