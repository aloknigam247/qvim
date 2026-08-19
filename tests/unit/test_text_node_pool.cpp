#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QQuickItem>
#include <QSGNode>
#include <QSGTextNode>
#include <QtTest>

#include "support/QuickRasterizer.h"

#include "TextNodePool.h"

using namespace qvim;

// The scene-graph port exists to get subpixel-antialiased glyphs (issue #15),
// and QSGTextNode::NativeRendering is the only render type that reaches the
// platform's subpixel rasteriser. No pixel test can catch a regression here:
// the whole suite runs on the software adaptation, where glyph nodes fall back
// through QPainter and every render type looks alike. So assert on the render
// type directly.
class TestTextNodePool : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QuickRasterizer::useSoftwareBackend();
        if(QGuiApplication::primaryScreen() == nullptr) QSKIP("No primary screen available.");
    }

    void usesNativeRenderingForSubpixelAntialiasing() {
        QuickRasterizer raster;
        QQuickItem item;
        item.setWidth(64);
        item.setHeight(32);
        // Renders once so the scene graph is initialised; createTextNode needs
        // a live renderer behind the window.
        QVERIFY(!raster.render(&item).isNull());

        QSGNode parent;
        TextNodePool pool;
        pool.beginFrame(&parent, raster.window());
        pool.addText(1, QStringLiteral("abc"), QFont(), QColor(Qt::white), QPointF(0, 10));
        pool.endFrame();

        QCOMPARE(pool.nodeCount(), 1);
        QVERIFY(pool.nodeAt(0) != nullptr);
        QCOMPARE(pool.nodeAt(0)->renderType(), QSGTextNode::NativeRendering);
    }

    void reusesNodesAcrossFramesWithTheSameKeys() {
        QuickRasterizer raster;
        QQuickItem item;
        item.setWidth(64);
        item.setHeight(32);
        QVERIFY(!raster.render(&item).isNull());

        QSGNode parent;
        TextNodePool pool;
        const QFont font;

        pool.beginFrame(&parent, raster.window());
        pool.addText(1, QStringLiteral("abc"), font, QColor(Qt::white), QPointF(0, 10));
        pool.addText(2, QStringLiteral("def"), font, QColor(Qt::red), QPointF(0, 20));
        pool.endFrame();
        QCOMPARE(pool.nodeCount(), 2);
        QSGTextNode *first = pool.nodeAt(0);

        // Same keys next frame: no new nodes, and the existing ones are reused
        // rather than rebuilt, which is what keeps a steady-state frame
        // allocation-free.
        pool.beginFrame(&parent, raster.window());
        pool.addText(1, QStringLiteral("xyz"), font, QColor(Qt::white), QPointF(0, 10));
        pool.addText(2, QStringLiteral("uvw"), font, QColor(Qt::red), QPointF(0, 20));
        pool.endFrame();
        QCOMPARE(pool.nodeCount(), 2);
        QCOMPARE(pool.nodeAt(0), first);

        // Fewer keys: the surplus node must be released, not leaked.
        pool.beginFrame(&parent, raster.window());
        pool.addText(1, QStringLiteral("abc"), font, QColor(Qt::white), QPointF(0, 10));
        pool.endFrame();
        QCOMPARE(pool.nodeCount(), 1);
    }
};

QTEST_MAIN(TestTextNodePool)
#include "test_text_node_pool.moc"
