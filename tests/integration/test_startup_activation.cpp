// Once-only startup-activation lifecycle guard for issue #69.
//
// qvim shipped no startup foreground/activation logic, so on launch its window
// could open behind other windows. The fix adds WindowChrome::activateOnShow(),
// a once-only hook that fires the first time the window becomes visible and
// brings it to the foreground.
//
// This test pins the *lifecycle* of that seam: activation runs exactly once on
// first show and never again on a later hide/show. It asserts on the
// activated() signal, never on an OS foreground query — under the minimal QPA
// there is no real window manager, so the actual foreground/MRU outcome is
// proven by the manual harness (tmp/repro-69-foreground.ps1), not here.

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QtTest>

#include "IntegrationHelpers.h"
#include "NvimConnector.h"
#include "WindowChrome.h"

using namespace qvim;
using namespace qvim::test;

namespace {

template <typename F>
bool waitUntil(F &&predicate, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while(!predicate()) {
        if(t.elapsed() >= timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return true;
}

} // namespace

class TestStartupActivation : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void activatesExactlyOnceOnFirstShow() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(80, 24));

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("$connector"), &conn);
        engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);

        // Install the hook while the window is still hidden, exactly as
        // main.cpp does after loadFromModule and before the async show.
        QVERIFY2(!window->isVisible(), "Window was visible immediately after load; the activation "
                                       "hook must be installed before the first show.");
        WindowChrome chrome;
        chrome.activateOnShow(window);
        QSignalSpy spy(&chrome, &WindowChrome::activated);

        // Drive the loop until Main.qml flips the window visible after the
        // post-attach resize flush.
        QVERIFY2(waitUntil([&]() { return window->isVisible(); }, 5000),
                 "Window never became visible — flush listener or safety timer in Main.qml "
                 "didn't fire.");
        QTest::qWait(50); // let the queued visibleChanged slot run

        QCOMPARE(spy.count(), 1);

        // A later hide/show must NOT re-activate — the guard is one-shot.
        window->hide();
        window->show();
        QTest::qWait(50);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestStartupActivation)
#include "test_startup_activation.moc"
