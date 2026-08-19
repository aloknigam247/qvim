#include <QKeyEvent>
#include <QtTest>

#include "InputHandler.h"
#include "IntegrationHelpers.h"

using namespace qvim;
using namespace qvim::test;

namespace {
QString encode(int key, Qt::KeyboardModifiers mods, const QString &text) {
    QKeyEvent ev(QEvent::KeyPress, key, mods, text);
    return InputHandler::keyToNvim(&ev);
}
} // namespace

// The unit tests pin the encoded *string*; this pins the thing the user
// actually cares about — that a `:nnoremap <C-S-i>` mapping fires when
// Ctrl+Shift+i is pressed, and that plain Ctrl+i does not trigger it.
class TestCtrlShiftMapping : public QObject {
    Q_OBJECT
private:
    static bool waitForRow0(NvimConnector &conn, const QString &prefix, int flushes = 8) {
        for(int i = 0; i < flushes; ++i) {
            if(conn.grid()->dumpAscii().startsWith(prefix)) return true;
            waitForFlush(&conn, 1000);
        }
        return conn.grid()->dumpAscii().startsWith(prefix);
    }

private slots:
    void ctrlShiftIFiresMapping() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(40, 10));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.command(QStringLiteral("nnoremap <C-S-i> ihit<Esc>"));
        waitForFlush(&conn, 1000);

        const QString keys =
            encode(Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("\x09"));
        QCOMPARE(keys, QStringLiteral("<S-C-i>"));

        conn.input(keys);
        QVERIFY2(waitForRow0(conn, QStringLiteral("hit")),
                 qPrintable(QStringLiteral("<C-S-i> mapping did not fire; row 0 = '%1'")
                                .arg(conn.grid()->dumpAscii().left(40))));
    }

    void plainCtrlIDoesNotFireMapping() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(40, 10));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.command(QStringLiteral("nnoremap <C-S-i> ihit<Esc>"));
        waitForFlush(&conn, 1000);

        const QString keys = encode(Qt::Key_I, Qt::ControlModifier, QStringLiteral("\x09"));
        QCOMPARE(keys, QStringLiteral("<C-i>"));

        conn.input(keys);
        for(int i = 0; i < 5; ++i) waitForFlush(&conn, 500);

        const QString dump = conn.grid()->dumpAscii();
        QVERIFY2(!dump.startsWith(QStringLiteral("hit")),
                 qPrintable(QStringLiteral("plain <C-i> wrongly fired the <C-S-i> mapping; "
                                           "row 0 = '%1'")
                                .arg(dump.left(40))));
    }
};

QTEST_GUILESS_MAIN(TestCtrlShiftMapping)
#include "test_ctrl_shift_mapping.moc"
