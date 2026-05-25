#include <QtTest>
#include "IntegrationHelpers.h"
#include "MessagesModel.h"

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

        MessagesModel* msgs = conn.messages();
        QVERIFY(msgs != nullptr);

        conn.command(QStringLiteral("echom \"hello\""));

        bool found = false;
        for (int i = 0; i < 20 && !found; ++i) {
            waitForFlush(&conn, 500);
            for (int r = 0; r < msgs->rowCount(); ++r) {
                const QString text =
                    msgs->data(msgs->index(r), MessagesModel::TextRole).toString();
                if (text.contains(QStringLiteral("hello"))) {
                    found = true;
                    break;
                }
            }
        }
        QVERIFY2(found, "MessagesModel did not receive 'hello' via msg_show");
    }
};

QTEST_GUILESS_MAIN(TestMessages)
#include "test_messages.moc"
