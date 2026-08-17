#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "SessionEventLog.h"

using namespace qvim;

namespace {
QJsonObject frame(const QString& text) {
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("message")},
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("text"), text},
    };
}

quint64 seqOf(const LoggedEvent& e) {
    return static_cast<quint64>(
        QJsonDocument::fromJson(e.json).object().value(QStringLiteral("seq")).toInteger());
}
} // namespace

class TestSessionEventLog : public QObject {
    Q_OBJECT

private slots:
    void assignsMonotonicSeqFromOne();
    void stampsSeqIntoSerialisedFrame();
    void boundedEvictionNeverReusesSeq();
};

void TestSessionEventLog::assignsMonotonicSeqFromOne() {
    SessionEventLog log;
    QCOMPARE(log.nextSeq(), quint64(1));

    const LoggedEvent e1 = log.append(frame(QStringLiteral("a")));
    const LoggedEvent e2 = log.append(frame(QStringLiteral("b")));
    const LoggedEvent e3 = log.append(frame(QStringLiteral("c")));

    QCOMPARE(e1.seq, quint64(1));
    QCOMPARE(e2.seq, quint64(2));
    QCOMPARE(e3.seq, quint64(3));
    QVERIFY(e2.seq > e1.seq);
    QVERIFY(e3.seq > e2.seq);
    QCOMPARE(log.nextSeq(), quint64(4));
    QCOMPARE(log.size(), 3);
}

void TestSessionEventLog::stampsSeqIntoSerialisedFrame() {
    SessionEventLog log;
    const LoggedEvent e = log.append(frame(QStringLiteral("hi")));
    // The wire bytes carry the assigned seq, not just the in-memory struct.
    QCOMPARE(seqOf(e), quint64(1));
    QCOMPARE(e.seq, quint64(1));
}

void TestSessionEventLog::boundedEvictionNeverReusesSeq() {
    SessionEventLog log(4);
    for (int i = 0; i < 10; ++i) {
        log.append(frame(QString::number(i)));
    }

    // Bound holds; the oldest six were evicted, leaving seq 7..10.
    QCOMPARE(log.size(), 4);
    QCOMPARE(log.entries().front().seq, quint64(7));
    QCOMPARE(log.entries().back().seq, quint64(10));
    QCOMPARE(log.nextSeq(), quint64(11));

    // The next append continues past the evicted range — a dropped seq is
    // never handed out again.
    const LoggedEvent next = log.append(frame(QStringLiteral("x")));
    QCOMPARE(next.seq, quint64(11));
    QCOMPARE(log.entries().front().seq, quint64(8));
    QCOMPARE(log.entries().back().seq, quint64(11));
    QCOMPARE(log.size(), 4);
}

QTEST_GUILESS_MAIN(TestSessionEventLog)
#include "test_session_event_log.moc"
