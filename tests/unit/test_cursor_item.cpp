#include <QtTest>

#include "CursorItem.h"
#include "ModeInfo.h"

using namespace qvim;

class TestCursorItem : public QObject {
    Q_OBJECT
private slots:
    // Block cursor: covers the full cell.
    void blockShapeFillsCell() {
        const QRectF r = CursorItem::cursorRectFor(3, 5, 8.0, 16.0, CursorShape::Block);
        QCOMPARE(r.x(),      5.0 * 8.0);
        QCOMPARE(r.y(),      3.0 * 16.0);
        QCOMPARE(r.width(),  8.0);
        QCOMPARE(r.height(), 16.0);
    }

    // Vertical cursor: 15% of cell width at the cell's left edge,
    // full cell height. Matches the pre-split GridItem behaviour.
    void verticalShapeIsLeftSliver() {
        const QRectF r = CursorItem::cursorRectFor(3, 5, 8.0, 16.0, CursorShape::Vertical);
        QCOMPARE(r.x(),      5.0 * 8.0);
        QCOMPARE(r.y(),      3.0 * 16.0);
        QCOMPARE(r.width(),  std::max(1.0, 8.0 * 0.15));
        QCOMPARE(r.height(), 16.0);
    }

    // Horizontal cursor: full cell width, 15% of cell height anchored to the
    // cell's bottom. Used for `guicursor=hor20` etc.
    void horizontalShapeIsBottomSliver() {
        const QRectF r = CursorItem::cursorRectFor(3, 5, 8.0, 16.0, CursorShape::Horizontal);
        const qreal expectedH = std::max(1.0, 16.0 * 0.15);
        QCOMPARE(r.x(),      5.0 * 8.0);
        QCOMPARE(r.width(),  8.0);
        QCOMPARE(r.height(), expectedH);
        // Bottom-anchored: y = row*cellHeight + cellHeight - expectedH
        QCOMPARE(r.y(),      3.0 * 16.0 + 16.0 - expectedH);
    }

    // For tiny cell sizes the 15% multiplier rounds below 1 pixel; clamp to 1
    // so the cursor is still visible. Validates the std::max(1.0, ...) clamp.
    void slimShapesClampToOnePixel() {
        const QRectF vert = CursorItem::cursorRectFor(0, 0, 4.0, 4.0, CursorShape::Vertical);
        const QRectF horiz = CursorItem::cursorRectFor(0, 0, 4.0, 4.0, CursorShape::Horizontal);
        // 4.0 * 0.15 = 0.6, clamped to 1.0
        QCOMPARE(vert.width(),   1.0);
        QCOMPARE(horiz.height(), 1.0);
    }

    // Rects from consecutive cursor positions must be unioned for the dirty
    // region passed to update(QRect), so the old cursor cell gets cleared and
    // the new one drawn in a single repaint. Direct math check on Qt's
    // QRectF::united — this is the operation CursorItem::scheduleRepaint uses.
    void unionedRectCoversBothCells() {
        const QRectF oldRect = CursorItem::cursorRectFor(3, 5, 8.0, 16.0, CursorShape::Block);
        const QRectF newRect = CursorItem::cursorRectFor(3, 6, 8.0, 16.0, CursorShape::Block);
        const QRect dirty = oldRect.united(newRect).toAlignedRect();
        QVERIFY(dirty.contains(oldRect.toAlignedRect()));
        QVERIFY(dirty.contains(newRect.toAlignedRect()));
        // Two adjacent cells in the same row: union should be exactly 16px wide.
        QCOMPARE(dirty.width(),  16);
        QCOMPARE(dirty.height(), 16);
    }
};

QTEST_GUILESS_MAIN(TestCursorItem)
#include "test_cursor_item.moc"
