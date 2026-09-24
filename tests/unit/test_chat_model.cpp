#include <QSignalSpy>
#include <QtTest>

#include "ChatModel.h"

using namespace qvim;

class TestChatModel : public QObject {
    Q_OBJECT
private slots:
    void appendBlockEmitsMessageAddedWithRoleAndText() {
        ChatModel m;
        QSignalSpy added(&m, &ChatModel::messageAdded);
        m.appendBlock(QStringLiteral("user"), QStringLiteral("hi"));
        QCOMPARE(m.count(), 1);
        QCOMPARE(m.authorAt(0), QStringLiteral("user"));
        QCOMPARE(m.textAt(0), QStringLiteral("hi"));
        QCOMPARE(added.count(), 1);
        const QList<QVariant> args = added.takeFirst();
        QVERIFY(!args.at(0).toString().isEmpty()); // stable per-block id
        QCOMPARE(args.at(1).toString(), QStringLiteral("user"));
        QCOMPARE(args.at(2).toString(), QStringLiteral("hi"));
    }

    void appendBlockAssignsDistinctIdsPerBlock() {
        ChatModel m;
        QSignalSpy added(&m, &ChatModel::messageAdded);
        m.appendBlock(QStringLiteral("user"), QStringLiteral("a"));
        m.appendBlock(QStringLiteral("assistant"), QStringLiteral("b"));
        QCOMPARE(added.count(), 2);
        const QString id0 = added.at(0).at(0).toString();
        const QString id1 = added.at(1).at(0).toString();
        QVERIFY(!id0.isEmpty());
        QVERIFY(id0 != id1);
        QCOMPARE(added.at(1).at(1).toString(), QStringLiteral("assistant"));
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
