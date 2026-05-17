#include "GridItem.h"
#include "NvimConnector.h"
#include "GridModel.h"
#include "HighlightTable.h"
#include "ModeInfo.h"
#include "InputHandler.h"

#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QRegularExpression>

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

GridItem::GridItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(false);
    setFlag(ItemHasContents, true);
    setFlag(ItemIsFocusScope, true);
    setActiveFocusOnTab(true);
    m_font = QFont(m_fontName, m_fontSize);
    m_font.setStyleHint(QFont::Monospace);
    m_font.setHintingPreference(QFont::PreferFullHinting);
    recomputeMetrics();
    connect(&m_blinkTimer, &QTimer::timeout, this, &GridItem::blinkTick);
    m_blinkTimer.setInterval(500);
    m_blinkTimer.start();
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
    const int cols = std::max(10, static_cast<int>(width()  / m_cellWidth));
    const int rows = std::max(3,  static_cast<int>(height() / m_cellHeight));
    if (auto* g = grid(); g && (g->cols() != cols || g->rows() != rows)) {
        m_conn->tryResize(cols, rows);
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
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->setFont(m_font);

    painter->fillRect(boundingRect(), h->defaultBg());

    const int cols = g->cols();
    const int rows = g->rows();

    for (int r = 0; r < rows; ++r) {
        const qreal y = r * m_cellHeight;
        const QRectF rowRect(0.0, y, m_cellWidth * cols, m_cellHeight);
        painter->save();
        painter->setClipRect(rowRect);

        int c = 0;
        while (c < cols) {
            const Cell& start = g->cell(r, c);
            const int runHl = start.hlId;
            int runEnd = c + 1;
            while (runEnd < cols && g->cell(r, runEnd).hlId == runHl) ++runEnd;

            HlAttr a = h->resolved(runHl);
            const QRectF runRect(c * m_cellWidth, y, (runEnd - c) * m_cellWidth, m_cellHeight);
            if (a.bg != h->defaultBg()) {
                painter->fillRect(runRect, a.bg);
            }

            QString runText;
            runText.reserve(runEnd - c);
            for (int cc = c; cc < runEnd; ++cc) {
                const Cell& cell = g->cell(r, cc);
                runText += cell.text.isEmpty() ? QChar(' ') : cell.text;
            }

            QFont rf = m_font;
            rf.setBold(a.bold);
            rf.setItalic(a.italic);
            rf.setUnderline(a.underline);
            rf.setStrikeOut(a.strikethrough);
            painter->setFont(rf);
            painter->setPen(a.fg);
            painter->drawText(QPointF(c * m_cellWidth, y + m_baseline), runText);

            if (a.undercurl) {
                painter->setPen(a.sp);
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
    }

    // Cursor
    if (m_cursorOn || !mode() || !mode()->cursorStyleEnabled()) {
        const int cr = g->cursorRow();
        const int cc = g->cursorCol();
        if (cr >= 0 && cr < rows && cc >= 0 && cc < cols) {
            HlAttr a = h->resolved(mode() ? mode()->attrId() : 0);
            QColor curColor = a.bg.isValid() ? a.bg : h->defaultFg();
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
                const Cell& cell = g->cell(cr, cc);
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
        const QString dump = g->dumpAscii();
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
    m_conn->inputMouse(m.button, m.action, m.modifier, 0, row, col);
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
    m_conn->inputMouse(QStringLiteral("wheel"), dir, QString(), 0, row, col);
    ev->accept();
}

} // namespace qvim
