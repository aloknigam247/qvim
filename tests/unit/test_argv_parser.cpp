#include <QtTest>
#include <QStringList>

#include <vector>
#include <string>

#include "ArgvParser.h"

using namespace qvim;

namespace {

// Build a fake argv array from a QStringList. Owns the underlying byte
// storage in `storage` so the returned char* array stays valid for the
// duration of the test scope.
struct FakeArgv {
    std::vector<std::string> storage;
    std::vector<char*>       ptrs;

    explicit FakeArgv(const QStringList& tokens) {
        storage.reserve(tokens.size());
        ptrs.reserve(tokens.size());
        for (const QString& t : tokens) {
            storage.push_back(t.toLocal8Bit().toStdString());
            ptrs.push_back(storage.back().data());
        }
    }

    int    argc() { return static_cast<int>(ptrs.size()); }
    char** argv() { return ptrs.data(); }
};

} // namespace

class TestArgvParser : public QObject {
    Q_OBJECT

private slots:
    void noArgs() {
        FakeArgv av{{QStringLiteral("qvim")}};
        const auto r = parseArgv(av.argc(), av.argv());
        QVERIFY(r.nvimForwardArgs.isEmpty());
        QVERIFY(!r.helpRequested);
        QVERIFY(!r.versionRequested);
    }

    void singleFile() {
        FakeArgv av{{QStringLiteral("qvim"), QStringLiteral("foo.txt")}};
        const auto r = parseArgv(av.argc(), av.argv());
        QCOMPARE(r.nvimForwardArgs, QStringList{QStringLiteral("foo.txt")});
    }

    void splitOptionMultipleFiles() {
        FakeArgv av{{QStringLiteral("qvim"), QStringLiteral("-O"),
                     QStringLiteral("a.txt"), QStringLiteral("b.txt")}};
        const auto r = parseArgv(av.argc(), av.argv());
        const QStringList expected{QStringLiteral("-O"),
                                   QStringLiteral("a.txt"),
                                   QStringLiteral("b.txt")};
        QCOMPARE(r.nvimForwardArgs, expected);
    }

    void plusLineNumber() {
        FakeArgv av{{QStringLiteral("qvim"), QStringLiteral("+10"),
                     QStringLiteral("foo.txt")}};
        const auto r = parseArgv(av.argc(), av.argv());
        const QStringList expected{QStringLiteral("+10"),
                                   QStringLiteral("foo.txt")};
        QCOMPARE(r.nvimForwardArgs, expected);
    }

    void dashCWithCommand() {
        FakeArgv av{{QStringLiteral("qvim"), QStringLiteral("-c"),
                     QStringLiteral("set number"),
                     QStringLiteral("foo.txt")}};
        const auto r = parseArgv(av.argc(), av.argv());
        const QStringList expected{QStringLiteral("-c"),
                                   QStringLiteral("set number"),
                                   QStringLiteral("foo.txt")};
        QCOMPARE(r.nvimForwardArgs, expected);
    }

    void longHelp() {
        FakeArgv av{{QStringLiteral("qvim"), QStringLiteral("--help")}};
        const auto r = parseArgv(av.argc(), av.argv());
        QVERIFY(r.helpRequested);
        QVERIFY(r.nvimForwardArgs.isEmpty());
    }

    void shortHelp() {
        FakeArgv av{{QStringLiteral("qvim"), QStringLiteral("-h")}};
        const auto r = parseArgv(av.argc(), av.argv());
        QVERIFY(r.helpRequested);
        QVERIFY(r.nvimForwardArgs.isEmpty());
    }

    void longVersion() {
        FakeArgv av{{QStringLiteral("qvim"), QStringLiteral("--version")}};
        const auto r = parseArgv(av.argc(), av.argv());
        QVERIFY(r.versionRequested);
        QVERIFY(r.nvimForwardArgs.isEmpty());
    }

    void shortVersion() {
        FakeArgv av{{QStringLiteral("qvim"), QStringLiteral("-v")}};
        const auto r = parseArgv(av.argc(), av.argv());
        QVERIFY(r.versionRequested);
        QVERIFY(r.nvimForwardArgs.isEmpty());
    }

    void pathWithSpaces() {
        const QString path = QStringLiteral("C:\\Program Files\\foo.txt");
        FakeArgv av{{QStringLiteral("qvim"), path}};
        const auto r = parseArgv(av.argc(), av.argv());
        QCOMPARE(r.nvimForwardArgs, QStringList{path});
    }

    void qvimReservedNamespaceSwallowed() {
        FakeArgv av{{QStringLiteral("qvim"),
                     QStringLiteral("--qvim-future-option"),
                     QStringLiteral("foo.txt")}};
        const auto r = parseArgv(av.argc(), av.argv());
        QCOMPARE(r.nvimForwardArgs, QStringList{QStringLiteral("foo.txt")});
    }
};

QTEST_GUILESS_MAIN(TestArgvParser)
#include "test_argv_parser.moc"
