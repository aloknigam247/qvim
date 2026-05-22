#pragma once

#include <QQuickPaintedItem>
#include <QFont>
#include <QFontMetricsF>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <qqmlregistration.h>

#include "NvimConnector.h"

namespace qvim {

class FontFallback;
class GridModel;
class HighlightTable;
class ModeInfo;

class GridItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qvim::NvimConnector* connector READ connector WRITE setConnector NOTIFY connectorChanged)
    Q_PROPERTY(int     gridId    READ gridId    WRITE setGridId    NOTIFY gridIdChanged)
    Q_PROPERTY(QString fontName  READ fontName  WRITE setFontName  NOTIFY fontChanged)
    Q_PROPERTY(qreal   fontSize  READ fontSize  WRITE setFontSize  NOTIFY fontChanged)
    Q_PROPERTY(qreal   cellWidth  READ cellWidth  NOTIFY fontChanged)
    Q_PROPERTY(qreal   cellHeight READ cellHeight NOTIFY fontChanged)
    Q_PROPERTY(bool    debugOverlay READ debugOverlay WRITE setDebugOverlay NOTIFY debugOverlayChanged)

public:
    explicit GridItem(QQuickItem* parent = nullptr);
    ~GridItem() override;

    void paint(QPainter* painter) override;

    NvimConnector* connector() const { return m_conn; }
    void setConnector(NvimConnector* c);

    int  gridId() const { return m_gridId; }
    void setGridId(int id);

    QString fontName() const { return m_fontName; }
    void setFontName(const QString& name);

    qreal fontSize() const { return m_fontSize; }
    void setFontSize(qreal pt);

    qreal cellWidth()  const { return m_cellWidth; }
    qreal cellHeight() const { return m_cellHeight; }

    bool debugOverlay() const { return m_debugOverlay; }
    void setDebugOverlay(bool v);

    Q_INVOKABLE int colAt(qreal x) const;
    Q_INVOKABLE int rowAt(qreal y) const;

signals:
    void connectorChanged();
    void gridIdChanged();
    void fontChanged();
    void debugOverlayChanged();

protected:
    void keyPressEvent(QKeyEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;
    void wheelEvent(QWheelEvent* ev) override;
    void geometryChange(const QRectF& newGeom, const QRectF& oldGeom) override;
    void focusInEvent(QFocusEvent* ev) override;

private slots:
    void onGuifontChanged();
    void onFlush();
    void blinkTick();

private:
    void recomputeMetrics();
    void maybeResizeUi();
    GridModel*      grid() const;
    HighlightTable* hl()   const;
    ModeInfo*       mode() const;
    void sendMouse(QMouseEvent* ev, QEvent::Type type);

    QPointer<NvimConnector> m_conn;
    int      m_gridId       = 1;
    QString  m_fontName     = QStringLiteral("JetBrains Mono Nerd Font");
    qreal    m_fontSize     = 14.0;
    QFont    m_font;
    qreal    m_cellWidth    = 8.0;
    qreal    m_cellHeight   = 16.0;
    qreal    m_baseline     = 12.0;
    bool     m_debugOverlay = false;
    bool     m_cursorOn     = true;
    QTimer   m_blinkTimer;
    std::unique_ptr<FontFallback> m_fallback;
};

} // namespace qvim
