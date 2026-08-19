#include <QGuiApplication>
#include <QIcon>
#include <QPixmap>
#include <QtTest>

#include "AppIcon.h"

class TestAppIcon : public QObject {
    Q_OBJECT
private slots:
    void setsNonNullWindowIcon();
    void rasterisesAt32px();
};

void TestAppIcon::setsNonNullWindowIcon() {
    qvim::setupApplicationIcon(*qGuiApp);
    const QIcon icon = QGuiApplication::windowIcon();
    QVERIFY(!icon.isNull());
}

void TestAppIcon::rasterisesAt32px() {
    qvim::setupApplicationIcon(*qGuiApp);
    const QIcon icon = QGuiApplication::windowIcon();
    // availableSizes() is empty for QRC-loaded icons backed by qico — but
    // pixmap() must still return a valid rasterisation when asked.
    const QPixmap pm = icon.pixmap(QSize(32, 32));
    QVERIFY(!pm.isNull());
    QVERIFY(pm.width() > 0);
    QVERIFY(pm.height() > 0);
}

QTEST_MAIN(TestAppIcon)
#include "test_app_icon.moc"
