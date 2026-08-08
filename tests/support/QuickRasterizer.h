#pragma once

// Offscreen rasteriser for a single QQuickItem, used by the pixel-probe tests.
//
// GridItem and CursorItem render through updatePaintNode, so there is no
// QPainter entry point a test can call. Tests used to call paint() directly;
// doing the equivalent now would mean writing a second, test-only renderer —
// exactly the arrangement where the suite stays green while the shipping
// renderer is broken.
//
// So this renders through the real scene graph and reads the pixels back. The
// window uses the software adaptation, the only backend whose grabWindow() is
// synchronous and needs no GPU context, and is positioned far off-screen so a
// test run does not flash windows over the desktop.
//
// Call QuickRasterizer::useSoftwareBackend() from initTestCase(), before any
// QQuickWindow exists — the backend is chosen once per process.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>

namespace qvim {

class QuickRasterizer {
public:
    static void useSoftwareBackend() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    // Renders one frame and returns its pixels. Returns a null QImage if the
    // scene graph produced nothing; callers must treat that as a hard failure,
    // since a silent null would make every pixel assertion below it vacuous.
    QImage render(QQuickItem* item) {
        const int w = static_cast<int>(item->width());
        const int h = static_cast<int>(item->height());
        item->setParentItem(m_window.contentItem());
        m_window.resize(w, h);
        // Deliberately never shown. With the software adaptation grabWindow()
        // renders a hidden window synchronously into a QImage; showing one
        // instead would flash windows across the desktop on every test run,
        // and a window pushed off-screen to avoid that is never exposed by
        // Windows, so the grab would come back empty.
        return m_window.grabWindow();
    }

    // True when one rendered pixel equals one logical pixel. Pixel-probe tests
    // address the image in logical coordinates and goldens are stored at 1:1,
    // so a scaled surface must fail the test rather than silently shift every
    // probe. CTest pins QT_ENABLE_HIGHDPI_SCALING=0 for those targets.
    bool isUnscaled() const {
        return qFuzzyCompare(m_window.effectiveDevicePixelRatio(), qreal(1));
    }

    QQuickWindow* window() { return &m_window; }

private:
    QQuickWindow m_window;
};

} // namespace qvim
