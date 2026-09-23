#include <QSignalSpy>
#include <QtTest>

#include "ChatModel.h"

using namespace qvim;

class TestChatModel : public QObject {
    Q_OBJECT
private slots:
    void submitAppendsUserBlockImmediately() {
        ChatModel m;
        QSignalSpy added(&m, &ChatModel::messageAdded);
        QSignalSpy began(&m, &ChatModel::messageBegan);
        m.submit(QStringLiteral("hi"));
        // User block lands synchronously; the assistant block begins empty and
        // streams asynchronously.
        QCOMPARE(m.count(), 2);
        QCOMPARE(m.authorAt(0), QStringLiteral("user"));
        QCOMPARE(m.textAt(0), QStringLiteral("hi"));
        QCOMPARE(added.count(), 1);
        QCOMPARE(began.count(), 1);
    }

    void whitespaceSubmitIsIgnored() {
        ChatModel m;
        m.submit(QStringLiteral("   "));
        QCOMPARE(m.count(), 0);
    }

    void streamedReplyAssemblesEcho() {
        ChatModel m;
        m.submit(QStringLiteral("ping"));
        QSignalSpy ended(&m, &ChatModel::messageEnded);
        QVERIFY(QTest::qWaitFor([&] { return ended.count() >= 1; }, 3000));
        QCOMPARE(m.authorAt(1), QStringLiteral("assistant"));
        QCOMPARE(m.textAt(1), QStringLiteral("Echo: ping"));
    }

    void appendBlockSystemAuthor() {
        ChatModel m;
        m.appendBlock(QStringLiteral("system"), QStringLiteral("note"));
        QCOMPARE(m.count(), 1);
        QCOMPARE(m.authorAt(0), QStringLiteral("system"));
    }

    void appendBlockUnknownAuthorFallsBackToSystem() {
        ChatModel m;
        m.appendBlock(QStringLiteral("robot"), QStringLiteral("beep"));
        QCOMPARE(m.authorAt(0), QStringLiteral("system"));
    }

    void appendBlockEmptyTextIsIgnored() {
        ChatModel m;
        m.appendBlock(QStringLiteral("user"), QString());
        QCOMPARE(m.count(), 0);
    }

    void dataReturnsInvalidForUnknownRoleAndOutOfRange() {
        ChatModel m;
        m.appendBlock(QStringLiteral("user"), QStringLiteral("x"));
        const QModelIndex idx = m.index(0, 0);
        QVERIFY(!m.data(idx, Qt::UserRole + 99).isValid());
        QVERIFY(!m.data(m.index(5, 0), ChatModel::TextRole).isValid());
    }

    void roleNamesExposeAuthorAndText() {
        ChatModel m;
        const auto roles = m.roleNames();
        QCOMPARE(roles.value(ChatModel::AuthorRole), QByteArrayLiteral("author"));
        QCOMPARE(roles.value(ChatModel::TextRole), QByteArrayLiteral("text"));
    }
};

QTEST_GUILESS_MAIN(TestChatModel)
#include "test_chat_model.moc"
