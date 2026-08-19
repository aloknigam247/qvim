#include "IntegrationHelpers.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace qvim;
using namespace qvim::test;

namespace {
bool waitFor(std::function<bool()> pred, int timeoutMs = 5000) {
    QElapsedTimer t;
    t.start();
    while(!pred()) {
        if(t.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}
} // namespace

class TestTitleUpdates : public QObject {
    Q_OBJECT
private slots:
    void titleReflectsEditedFile() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(40, 10));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString filePath = QDir(tmp.path()).filePath(QStringLiteral("foo.txt"));
        {
            QFile f(filePath);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("hello\n");
            f.close();
        }

        QSignalSpy titleSpy(&conn, &NvimConnector::titleChanged);

        // Use forward-slash form to avoid escaping issues in nvim_command.
        QString cmd = QStringLiteral("edit ") + QDir::fromNativeSeparators(filePath);
        conn.command(cmd);

        QVERIFY2(
            waitFor([&]() { return conn.title().contains(QStringLiteral("foo.txt")); }, 5000),
            qPrintable(QStringLiteral("title never contained 'foo.txt': '%1'").arg(conn.title())));
        QVERIFY(titleSpy.count() >= 1);

        // Dirty the buffer; expect the modified marker ("+") to appear.
        conn.input(QStringLiteral("ix<Esc>"));
        QVERIFY2(waitFor([&]() { return conn.title().contains(QLatin1Char('+')); }, 5000),
                 qPrintable(QStringLiteral("title never contained '+': '%1'").arg(conn.title())));

        conn.command(QStringLiteral("qa!"));
    }
};

QTEST_GUILESS_MAIN(TestTitleUpdates)
#include "test_title_updates.moc"
