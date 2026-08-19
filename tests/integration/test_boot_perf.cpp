// Boot-time regression guard. Times the engine.loadFromModule + nvim attach +
// first flush path from within a single test process. This is NOT a true cold
// launch — QGuiApplication is already up and Qt DLLs are already mapped — but
// it captures the cost we control: QML compile/load, the nvim --embed spawn,
// nvim_ui_attach handshake, and the first redraw flush. A regression in any
// of those phases (e.g. accidentally serialising the nvim spawn behind QML
// load again) will trip the ceiling assert.
//
// Skip with QVIM_SKIP_PERF=1 on machines where the absolute timing is unstable
// (CI sometimes spawns nvim very slowly).

#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QtTest>

#include "IntegrationHelpers.h"
#include "NvimConnector.h"

using namespace qvim;
using namespace qvim::test;

class TestBootPerf : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void timeToFirstFlush() {
        if(qEnvironmentVariableIntValue("QVIM_SKIP_PERF") != 0) { QSKIP("QVIM_SKIP_PERF set"); }

        QElapsedTimer t;
        t.start();

        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("$connector"), &conn);
        engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        const qint64 elapsed = t.elapsed();
        qDebug() << "Boot to first flush:" << elapsed << "ms";

        // Ceiling chosen with headroom over observed values. The test runs in
        // a process that already has Qt loaded, so this is well below a true
        // cold-launch budget; a regression that doubles cost will still trip
        // this. If the machine is slow (e.g. busy CI), set QVIM_SKIP_PERF=1.
        QVERIFY2(elapsed < 3000,
                 qPrintable(QStringLiteral("Boot took %1 ms — perf regression").arg(elapsed)));
    }
};

QTEST_MAIN(TestBootPerf)
#include "test_boot_perf.moc"
