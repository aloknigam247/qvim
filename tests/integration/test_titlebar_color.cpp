#include <QColor>
#include <QSignalSpy>
#include <QtTest>

#include "IntegrationHelpers.h"
#include "HighlightTable.h"
#include "NvimConnector.h"

using namespace qvim;
using namespace qvim::test;

class TestTitlebarColor : public QObject {
    Q_OBJECT
private slots:
    void defaultBackgroundReflectsHiNormal() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        QSignalSpy spy(&conn, &NvimConnector::defaultBackgroundChanged);

        conn.command(QStringLiteral("hi Normal guibg=#123456"));

        QVERIFY2(spy.wait(5000),
                 "defaultBackgroundChanged did not fire after :hi Normal guibg");

        QCOMPARE(conn.defaultBackground(), QColor("#123456"));
        QCOMPARE(conn.highlights()->defaultBg(), QColor("#123456"));
    }
};

QTEST_GUILESS_MAIN(TestTitlebarColor)
#include "test_titlebar_color.moc"
