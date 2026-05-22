// Integration test for Nerd Font glyph rendering. Reproduces the bug Alok
// hit: PUA codepoints (e.g. powerline triangle U+E0B0) were rendering as tofu
// because QPainter::drawText let Qt's text engine substitute the configured
// family to a fallback that lacked the glyph. The FontFallback resolver +
// QGlyphRun path bypasses that substitution.
//
// We can't bake a specific Nerd Font into CI; instead we probe for any
// installed family with "nerd" in the name (or known substitutes like
// "Symbols Nerd Font"). If none is installed, QSKIP cleanly — the unit test
// covers the resolver logic deterministically.

#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QRawFont>
#include <QSGRendererInterface>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QtTest>

#include "GridItem.h"
#include "IntegrationHelpers.h"
#include "NvimConnector.h"

using namespace qvim;
using namespace qvim::test;

namespace {

QString findInstalledNerdFont() {
    for (const QString& fam : QFontDatabase::families()) {
        if (fam.contains(QStringLiteral("Nerd"), Qt::CaseInsensitive)) return fam;
    }
    return {};
}

// PUA codepoint that virtually every Nerd Font carries (powerline arrow).
constexpr char32_t kProbePua = 0xE0B0;

QQuickWindow* loadMainQml(QQmlApplicationEngine& engine, NvimConnector* conn) {
    engine.rootContext()->setContextProperty(QStringLiteral("$connector"), conn);
    engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return nullptr;
    return qobject_cast<QQuickWindow*>(engine.rootObjects().first());
}

template <typename F>
bool waitUntil(F&& predicate, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (!predicate()) {
        if (t.elapsed() >= timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return true;
}

QImage grabItem(QQuickItem* item, int timeoutMs = 2000) {
    QSharedPointer<QQuickItemGrabResult> result = item->grabToImage();
    if (!result) return {};
    QElapsedTimer t;
    t.start();
    while (result->image().isNull() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return result->image();
}

// Count distinct pixel colours in the (col, row) cell of the rendered grid.
// cellW/cellH are taken from GridItem so the math matches the renderer.
int distinctColoursInCell(const QImage& img, int col, int row, qreal cellW, qreal cellH) {
    const int x0 = qRound(col * cellW);
    const int y0 = qRound(row * cellH);
    const int x1 = qMin(img.width(),  qRound((col + 1) * cellW));
    const int y1 = qMin(img.height(), qRound((row + 1) * cellH));
    QSet<QRgb> colours;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            colours.insert(img.pixel(x, y));
        }
    }
    return colours.size();
}

} // namespace

class TestNerdFontRender : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void puaGlyphRendersAsNonBlankCell() {
        const QString nerdFamily = findInstalledNerdFont();
        if (nerdFamily.isEmpty()) {
            QSKIP("No Nerd Font family installed on this host; skipping.");
        }
        // Confirm the family actually carries the probe glyph; otherwise we'd
        // be asserting on tofu from a misnamed font.
        {
            QFont f(nerdFamily); f.setPixelSize(14);
            if (!QRawFont::fromFont(f).supportsCharacter(kProbePua)) {
                QSKIP("Installed Nerd Font does not carry U+E0B0.");
            }
        }

        NvimConnector conn;
        QVERIFY(conn.start(locateNvim()));

        QQmlApplicationEngine engine;
        QQuickWindow* window = loadMainQml(engine, &conn);
        QVERIFY2(window, "Main.qml failed to load");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        // Locate the GridItem for grid 1.
        GridItem* gridItem = window->findChild<GridItem*>();
        QVERIFY2(gridItem, "Could not find GridItem in window tree");

        // Set the font to the Nerd family so its primary face will carry the
        // PUA glyph. This drives the same path users take via guifont.
        gridItem->setFontName(nerdFamily);

        // Put an ASCII 'X' followed by the PUA char at row 0. We use :put to
        // insert known content; this populates the grid deterministically.
        // The line content lands at row 0, col 0..1 after redraw.
        const QString cmd = QStringLiteral(":call setline(1, 'X' .. nr2char(0xE0B0))\n");
        conn.input(cmd);
        QVERIFY(waitForFlush(&conn));
        // Give a few flushes for the line to land.
        QTest::qWait(200);

        const QImage frame = grabItem(gridItem);
        QVERIFY2(!frame.isNull() && frame.width() > 0 && frame.height() > 0,
                 "grabItem returned an empty image");

        const qreal cw = gridItem->cellWidth();
        const qreal ch = gridItem->cellHeight();
        QVERIFY(cw > 0 && ch > 0);

        const int asciiColours = distinctColoursInCell(frame, 0, 0, cw, ch);
        const int puaColours   = distinctColoursInCell(frame, 1, 0, cw, ch);

        QVERIFY2(puaColours > 1,
                 qPrintable(QStringLiteral("PUA cell rendered as a single colour "
                                           "(blank/tofu): %1 distinct colours")
                                .arg(puaColours)));

        // Defence in depth: the PUA cell must not be a clone of the 'X' cell
        // (which would mean both rendered the same tofu / same letter). They
        // need not be identical *colour sets*, but they should differ.
        QSet<QRgb> asciiSet, puaSet;
        const int x0a = 0, y0 = 0;
        const int x0b = qRound(cw);
        const int xEndA = qRound(cw), xEndB = qMin(frame.width(), qRound(2 * cw));
        const int yEnd = qMin(frame.height(), qRound(ch));
        for (int y = y0; y < yEnd; ++y) {
            for (int x = x0a; x < xEndA; ++x) asciiSet.insert(frame.pixel(x, y));
            for (int x = x0b; x < xEndB; ++x) puaSet.insert(frame.pixel(x, y));
        }
        QVERIFY2(asciiSet != puaSet,
                 qPrintable(QStringLiteral(
                     "PUA cell pixels identical to ASCII cell (%1 vs %2) — "
                     "likely both rendered as tofu via the same fallback")
                                .arg(asciiColours).arg(puaColours)));
    }
};

QTEST_MAIN(TestNerdFontRender)
#include "test_nerdfont_render.moc"
