#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "SessionEventBuffer.h"

using namespace qvim;

namespace {
QJsonObject frame(const QString &text) {
    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("message") },
        { QStringLiteral("role"), QStringLiteral("user") },
        { QStringLiteral("text"), text },
    };
}

quint64 seqOf(const BufferedEvent &e) {
    return static_cast<quint64>(
        QJsonDocument::fromJson(e.json).object().value(QStringLiteral("seq")).toInteger());
}
} // namespace

class TestSessionEventBuffer : public QObject {
    Q_OBJECT

private slots:
    void assignsMonotonicSeqFromOne();
    void stampsSeqIntoSerialisedFrame();
    void boundedEvictionNeverReusesSeq();
};

void TestSessionEventBuffer::assignsMonotonicSeqFromOne() {
    SessionEventBuffer buf;
    QCOMPARE(buf.nextSeq(), quint64(1));

    const BufferedEvent e1 = buf.append(frame(QStringLiteral("a")));
    const BufferedEvent e2 = buf.append(frame(QStringLiteral("b")));
    const BufferedEvent e3 = buf.append(frame(QStringLiteral("c")));

    QCOMPARE(e1.seq, quint64(1));
    QCOMPARE(e2.seq, quint64(2));
    QCOMPARE(e3.seq, quint64(3));
    QVERIFY(e2.seq > e1.seq);
    QVERIFY(e3.seq > e2.seq);
    QCOMPARE(buf.nextSeq(), quint64(4));
    QCOMPARE(buf.size(), 3);
}

void TestSessionEventBuffer::stampsSeqIntoSerialisedFrame() {
    SessionEventBuffer buf;
    const BufferedEvent e = buf.append(frame(QStringLiteral("hi")));
    // The wire bytes carry the assigned seq, not just the in-memory struct.
    QCOMPARE(seqOf(e), quint64(1));
    QCOMPARE(e.seq, quint64(1));
}

void TestSessionEventBuffer::boundedEvictionNeverReusesSeq() {
    SessionEventBuffer buf(4);
    for(int i = 0; i < 10; ++i) { buf.append(frame(QString::number(i))); }

    // Bound holds; the oldest six were evicted, leaving seq 7..10.
    QCOMPARE(buf.size(), 4);
    QCOMPARE(buf.entries().front().seq, quint64(7));
    QCOMPARE(buf.entries().back().seq, quint64(10));
    QCOMPARE(buf.nextSeq(), quint64(11));

    // The next append continues past the evicted range — a dropped seq is
    // never handed out again.
    const BufferedEvent next = buf.append(frame(QStringLiteral("x")));
    QCOMPARE(next.seq, quint64(11));
    QCOMPARE(buf.entries().front().seq, quint64(8));
    QCOMPARE(buf.entries().back().seq, quint64(11));
    QCOMPARE(buf.size(), 4);
}

QTEST_GUILESS_MAIN(TestSessionEventBuffer)
#include "test_session_event_buffer.moc"
