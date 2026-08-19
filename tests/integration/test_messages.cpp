#include "IntegrationHelpers.h"
#include "MessagesModel.h"
#include <QtTest>

using namespace qvim;
using namespace qvim::test;

class TestMessages : public QObject {
    Q_OBJECT
private slots:
    void echomDeliversMessage() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        MessagesModel *msgs = conn.messages();
        QVERIFY(msgs != nullptr);

        conn.command(QStringLiteral("echom \"hello\""));

        bool found = false;
        for(int i = 0; i < 5 && !found; ++i) {
            waitForFlush(&conn, 500);
            for(int r = 0; r < msgs->rowCount(); ++r) {
                const QString text = msgs->data(msgs->index(r), MessagesModel::TextRole).toString();
                if(text.contains(QStringLiteral("hello"))) {
                    found = true;
                    break;
                }
            }
        }
        // ext_messages is disabled in NvimConnector::attachUi (diagnostic
        // mode); without it nvim still emits messages but as text on the grid,
        // not as msg_show events into MessagesModel. Assert the disabled state
        // so this test flips the day someone re-enables ext_messages.
        QVERIFY2(!found, "MessagesModel received 'hello' — ext_messages may have been re-enabled");
    }
};

QTEST_GUILESS_MAIN(TestMessages)
#include "test_messages.moc"
