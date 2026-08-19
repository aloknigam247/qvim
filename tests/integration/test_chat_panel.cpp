// Tier 2 — chat panel end-to-end through the real Main.qml scene and a live
// nvim. Drives input via QTest::mouseClick / QTest::keyClick through Qt's focus
// chain (never NvimConnector::input directly) so a focus-handoff regression
// actually fails the test. Verifies, in order:
//   1. panel hidden by default; clicking the toggle button opens it and moves
//      focus to the chat input;
//   2. typing in the chat streams "Echo: hi" into an assistant block;
//   3. opening the panel actually shrinks the nvim grid (proves the anchor ->
//      geometryChange -> requestResize path, not a direct geometry poke);
//   4. the panel subtree renders (>1 distinct colour);
//   5. Escape closes the panel, restores grid columns, and hands focus back to
//      the grid so keystrokes reach nvim again.

#include <QColor>
#include <QHash>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSharedPointer>
#include <QtTest>

#include "ChatModel.h"
#include "IntegrationHelpers.h"
#include "NvimConnector.h"

using namespace qvim;
using namespace qvim::test;

namespace {

QQuickWindow *loadMainQml(QQmlApplicationEngine &engine, NvimConnector *conn) {
    engine.rootContext()->setContextProperty(QStringLiteral("$connector"), conn);
    engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
    if(engine.rootObjects().isEmpty()) return nullptr;
    return qobject_cast<QQuickWindow *>(engine.rootObjects().first());
}

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

QImage grabItem(QQuickItem *item, int timeoutMs = 2000) {
    QSharedPointer<QQuickItemGrabResult> result = item->grabToImage();
    if(!result) return {};
    QElapsedTimer t;
    t.start();
    while(result->image().isNull() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return result->image();
}

int distinctColours(const QImage &img) {
    QHash<QRgb, int> seen;
    for(int y = 0; y < img.height(); ++y) {
        for(int x = 0; x < img.width(); ++x) {
            seen.insert(img.pixel(x, y), 1);
            if(seen.size() > 1) return seen.size();
        }
    }
    return seen.size();
}

void clickItem(QQuickWindow *window, QQuickItem *item) {
    const QPointF c = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
    QTest::mouseClick(window, Qt::LeftButton, {}, c.toPoint());
}

} // namespace

class TestChatPanel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void chatPanelEndToEnd() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        QQuickWindow *window = loadMainQml(engine, &conn);
        QVERIFY2(window, "Main.qml failed to load");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        auto *toggle = window->findChild<QQuickItem *>(QStringLiteral("chatToggle"));
        auto *panel = window->findChild<QQuickItem *>(QStringLiteral("chatPanel"));
        auto *model = window->findChild<ChatModel *>();
        QVERIFY2(toggle, "chatToggle button not found in scene");
        QVERIFY2(panel, "chatPanel not found in scene");
        QVERIFY2(model, "ChatModel not found in scene");

        // Focus starts on the grid; record it so we can prove it is restored.
        QQuickItem *gridFocus = window->activeFocusItem();
        QVERIFY2(gridFocus != nullptr, "No active focus item after attach");
        QVERIFY(!panel->isVisible());
        const int baseCols = conn.grid()->gridCols(1);
        QVERIFY(baseCols > 0);

        // (1) Click the toggle -> panel opens and the chat input takes focus.
        clickItem(window, toggle);
        QVERIFY2(waitUntil([&] { return panel->isVisible(); }, 3000),
                 "Panel did not become visible after clicking the toggle");
        QVERIFY2(waitUntil(
                     [&] {
            QQuickItem *f = window->activeFocusItem();
            return f && f->objectName() == QStringLiteral("chatInput");
        }, 3000),
                 "Chat input did not take focus on open");

        // (3) Opening the panel shrinks the nvim grid via the resize path.
        QVERIFY2(waitUntil([&] { return conn.grid()->gridCols(1) < baseCols; }, 3000),
                 "Grid columns did not shrink when the panel opened");

        // (2) Type into the chat input via the focus chain -> streamed echo.
        QTest::keyClick(window, Qt::Key_H);
        QTest::keyClick(window, Qt::Key_I);
        QTest::keyClick(window, Qt::Key_Return);
        QVERIFY2(waitUntil([&] { return model->count() == 2; }, 3000),
                 "Chat did not receive the typed message (focus handoff on open?)");
        QCOMPARE(model->authorAt(0), QStringLiteral("user"));
        QCOMPARE(model->textAt(0), QStringLiteral("hi"));
        QCOMPARE(model->authorAt(1), QStringLiteral("assistant"));
        QVERIFY2(waitUntil([&] { return model->textAt(1) == QStringLiteral("Echo: hi"); }, 3000),
                 "Assistant block did not stream to 'Echo: hi'");

        // (4) The panel subtree actually renders.
        const QImage shot = grabItem(panel);
        QVERIFY2(!shot.isNull(), "grabToImage returned a null image for the panel");
        QVERIFY2(distinctColours(shot) > 1,
                 "Panel rendered a single flat colour (black-on-black / opacity-0 / z-order?)");

        // (5) Escape closes the panel (real Keys.onEscapePressed binding),
        // restores grid columns, and returns focus to the grid.
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY2(waitUntil([&] { return !panel->isVisible(); }, 3000),
                 "Panel did not close on Escape");
        QVERIFY2(waitUntil([&] { return conn.grid()->gridCols(1) == baseCols; }, 3000),
                 "Grid columns did not restore after closing the panel");
        QVERIFY2(waitUntil([&] { return window->activeFocusItem() == gridFocus; }, 3000),
                 "Focus was not handed back to the grid after closing the panel");

        // Keystrokes reach nvim again: enter insert mode, type 'x', escape.
        QTest::keyClick(window, Qt::Key_I);
        QTest::keyClick(window, Qt::Key_X);
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY2(
            waitUntil([&] { return conn.grid()->dumpAscii().contains(QLatin1Char('x')); }, 3000),
            "nvim did not receive keystrokes after the panel closed");
    }
};

QTEST_MAIN(TestChatPanel)
#include "test_chat_panel.moc"
