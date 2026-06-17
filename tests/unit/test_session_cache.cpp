#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QtTest>

#include "SessionCache.h"

using namespace qvim;

class TestSessionCache : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Redirect AppDataLocation to a temp dir so tests don't touch real config.
        QStandardPaths::setTestModeEnabled(true);
        // Clean any leftover from previous test runs.
        QFile::remove(cachePath());
    }

    void cleanupTestCase() {
        QFile::remove(cachePath());
        QStandardPaths::setTestModeEnabled(false);
    }

    void loadReturnsInvalidWhenMissing() {
        QFile::remove(cachePath());
        const SessionCache c = SessionCache::load();
        QVERIFY(!c.isValid());
        QCOMPARE(c.cols, 0);
        QCOMPARE(c.rows, 0);
        QVERIFY(c.guifont.isEmpty());
    }

    void saveAndLoadRoundTrips() {
        SessionCache toSave;
        toSave.guifont = QStringLiteral("Cascadia Code:h11");
        toSave.cols = 142;
        toSave.rows = 38;
        SessionCache::save(toSave);

        const SessionCache loaded = SessionCache::load();
        QVERIFY(loaded.isValid());
        QCOMPARE(loaded.guifont, toSave.guifont);
        QCOMPARE(loaded.cols, toSave.cols);
        QCOMPARE(loaded.rows, toSave.rows);
    }

    void loadReturnsInvalidOnCorruptFile() {
        QFile f(cachePath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("not json at all {{{");
        f.close();

        const SessionCache c = SessionCache::load();
        QVERIFY(!c.isValid());
    }

    void emptyGuifontIsValid() {
        // Users without set guifont should still cache cols/rows.
        SessionCache toSave;
        toSave.guifont = QString();
        toSave.cols = 100;
        toSave.rows = 30;
        SessionCache::save(toSave);

        const SessionCache loaded = SessionCache::load();
        QVERIFY(loaded.isValid());
        QVERIFY(loaded.guifont.isEmpty());
        QCOMPARE(loaded.cols, 100);
        QCOMPARE(loaded.rows, 30);
    }

private:
    static QString cachePath() {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        return QDir(dir).filePath(QStringLiteral("session.json"));
    }
};

QTEST_GUILESS_MAIN(TestSessionCache)
#include "test_session_cache.moc"
