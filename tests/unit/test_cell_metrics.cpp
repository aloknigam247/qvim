// Verifies computeCellMetrics snaps cell width/height to integer pixels so
// adjacent cell rects share an exact pixel boundary (no antialiased seam
// between selected rows in visual mode).

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QtTest>
#include <cmath>

#include "CellMetrics.h"

using namespace qvim;

namespace {

bool isIntegerValued(qreal v) {
    return v == std::round(v);
}

} // namespace

class TestCellMetrics : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (QGuiApplication::primaryScreen() == nullptr) {
            QSKIP("No primary screen available (headless without minimal QPA).");
        }
    }

    void widthAndHeightAreIntegers_data() {
        QTest::addColumn<QString>("family");
        QTest::addColumn<qreal>("pointSize");
        QTest::addColumn<int>("linespace");
        QTest::newRow("courier-12-ls0")        << QStringLiteral("Courier New") << qreal(12.0)  << 0;
        QTest::newRow("courier-12-ls1")        << QStringLiteral("Courier New") << qreal(12.0)  << 1;
        QTest::newRow("courier-12-ls8")        << QStringLiteral("Courier New") << qreal(12.0)  << 8;
        // Fractional point size produces fractional QFontMetricsF values on
        // typical hosts — the snap is exactly what we're verifying here.
        QTest::newRow("courier-11.3-ls0")      << QStringLiteral("Courier New") << qreal(11.3)  << 0;
        QTest::newRow("courier-11.3-ls3")      << QStringLiteral("Courier New") << qreal(11.3)  << 3;
        QTest::newRow("consolas-13.7-ls0")     << QStringLiteral("Consolas")    << qreal(13.7)  << 0;
    }

    void widthAndHeightAreIntegers() {
        QFETCH(QString, family);
        QFETCH(qreal,   pointSize);
        QFETCH(int,     linespace);

        QFont font(family);
        font.setPointSizeF(pointSize);
        const QFontMetricsF fm(font);

        const CellMetrics cm = computeCellMetrics(fm, linespace);

        QVERIFY2(isIntegerValued(cm.cellWidth),
            qPrintable(QStringLiteral("cellWidth %1 is not integer").arg(cm.cellWidth)));
        QVERIFY2(isIntegerValued(cm.cellHeight),
            qPrintable(QStringLiteral("cellHeight %1 is not integer").arg(cm.cellHeight)));

        // Sanity: a zero-pixel cell would crash hit-test (division by zero in
        // colAt/rowAt) and produce no visible glyph.
        QVERIFY2(cm.cellWidth  >= 1.0, "cellWidth must be >= 1");
        QVERIFY2(cm.cellHeight >= 1.0, "cellHeight must be >= 1");

        QVERIFY2(cm.baseline >= 0.0,            "baseline must be >= 0");
        QVERIFY2(cm.baseline <= cm.cellHeight,  "baseline must be <= cellHeight");
    }

    void linespaceMonotonicallyGrowsCellHeight() {
        QFont font(QStringLiteral("Courier New"));
        font.setPointSizeF(12.0);
        const QFontMetricsF fm(font);

        const qreal h0 = computeCellMetrics(fm, 0).cellHeight;
        const qreal h4 = computeCellMetrics(fm, 4).cellHeight;
        const qreal h8 = computeCellMetrics(fm, 8).cellHeight;
        QVERIFY2(h4 >= h0, "linespace=4 must not shrink cellHeight vs linespace=0");
        QVERIFY2(h8 >= h4, "linespace=8 must not shrink cellHeight vs linespace=4");
    }

    void negativeLinespaceIsClampedToZero() {
        QFont font(QStringLiteral("Courier New"));
        font.setPointSizeF(12.0);
        const QFontMetricsF fm(font);

        const CellMetrics cm0 = computeCellMetrics(fm,  0);
        const CellMetrics cmN = computeCellMetrics(fm, -5);
        QCOMPARE(cmN.cellWidth,  cm0.cellWidth);
        QCOMPARE(cmN.cellHeight, cm0.cellHeight);
        QCOMPARE(cmN.baseline,   cm0.baseline);
    }
};

QTEST_MAIN(TestCellMetrics)
#include "test_cell_metrics.moc"
