#include <QStringList>
#include <QtTest>

#include <string>
#include <vector>

#include "ArgvParser.h"

using namespace qvim;

namespace {

// Build a fake argv array from a QStringList. Owns the underlying byte
// storage in `storage` so the returned char* array stays valid for the
// duration of the test scope.
struct FakeArgv {
    std::vector<std::string> storage;
    std::vector<char *> ptrs;

    explicit FakeArgv(const QStringList &tokens) {
        storage.reserve(tokens.size());
        ptrs.reserve(tokens.size());
        for(const QString &t: tokens) {
            storage.push_back(t.toLocal8Bit().toStdString());
            ptrs.push_back(storage.back().data());
        }
    }

    int argc() { return static_cast<int>(ptrs.size()); }
    char **argv() { return ptrs.data(); }
};

} // namespace

class TestArgvParser : public QObject {
    Q_OBJECT

private slots:
    void noArgs() {
        FakeArgv av{ { QStringLiteral("qvim") } };
        const auto r = parseArgv(av.argc(), av.argv());
        QVERIFY(r.nvimForwardArgs.isEmpty());
        QVERIFY(!r.helpRequested);
        QVERIFY(!r.versionRequested);
    }

    void singleFile() {
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("foo.txt") } };
        const auto r = parseArgv(av.argc(), av.argv());
        QCOMPARE(r.nvimForwardArgs, QStringList{ QStringLiteral("foo.txt") });
    }

    void splitOptionMultipleFiles() {
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("-O"), QStringLiteral("a.txt"),
                       QStringLiteral("b.txt") } };
        const auto r = parseArgv(av.argc(), av.argv());
        const QStringList expected{ QStringLiteral("-O"), QStringLiteral("a.txt"),
                                    QStringLiteral("b.txt") };
        QCOMPARE(r.nvimForwardArgs, expected);
    }

    void plusLineNumber() {
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("+10"), QStringLiteral("foo.txt") } };
        const auto r = parseArgv(av.argc(), av.argv());
        const QStringList expected{ QStringLiteral("+10"), QStringLiteral("foo.txt") };
        QCOMPARE(r.nvimForwardArgs, expected);
    }

    void dashCWithCommand() {
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("-c"), QStringLiteral("set number"),
                       QStringLiteral("foo.txt") } };
        const auto r = parseArgv(av.argc(), av.argv());
        const QStringList expected{ QStringLiteral("-c"), QStringLiteral("set number"),
                                    QStringLiteral("foo.txt") };
        QCOMPARE(r.nvimForwardArgs, expected);
    }

    void longHelp() {
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("--help") } };
        const auto r = parseArgv(av.argc(), av.argv());
        QVERIFY(r.helpRequested);
        QVERIFY(r.nvimForwardArgs.isEmpty());
    }

    void shortHelp() {
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("-h") } };
        const auto r = parseArgv(av.argc(), av.argv());
        QVERIFY(r.helpRequested);
        QVERIFY(r.nvimForwardArgs.isEmpty());
    }

    void longVersion() {
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("--version") } };
        const auto r = parseArgv(av.argc(), av.argv());
        QVERIFY(r.versionRequested);
        QVERIFY(r.nvimForwardArgs.isEmpty());
    }

    void shortVersion() {
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("-v") } };
        const auto r = parseArgv(av.argc(), av.argv());
        QVERIFY(r.versionRequested);
        QVERIFY(r.nvimForwardArgs.isEmpty());
    }

    void pathWithSpaces() {
        const QString path = QStringLiteral("C:\\Program Files\\foo.txt");
        FakeArgv av{ { QStringLiteral("qvim"), path } };
        const auto r = parseArgv(av.argc(), av.argv());
        QCOMPARE(r.nvimForwardArgs, QStringList{ path });
    }

    void qvimReservedNamespaceSwallowed() {
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("--qvim-future-option"),
                       QStringLiteral("foo.txt") } };
        const auto r = parseArgv(av.argc(), av.argv());
        QCOMPARE(r.nvimForwardArgs, QStringList{ QStringLiteral("foo.txt") });
    }

    // PowerShell 7 leaves outer double-quotes on argv entries containing `.`,
    // so `.\tasks.md` arrives as `".\tasks.md"`. Strip the matching pair so
    // the file path forwarded to nvim is the bare path, not a quoted literal.
    void stripsOuterQuotesFromQuotedPath() {
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("\".\\tasks.md\"") } };
        const auto r = parseArgv(av.argc(), av.argv());
        QCOMPARE(r.nvimForwardArgs, QStringList{ QStringLiteral(".\\tasks.md") });
    }

    // pwsh's worst case: it crushes ALL args into a single quoted token like
    //   ".\tasks.md" "-O" ".\CLAUDE.md"
    // Detect the `" "` separator pattern and split back into the original args.
    void splitsPwshMangledMultiArg() {
        FakeArgv av{ { QStringLiteral("qvim"),
                       QStringLiteral("\".\\tasks.md\" \"-O\" \".\\CLAUDE.md\"") } };
        const auto r = parseArgv(av.argc(), av.argv());
        const QStringList expected{ QStringLiteral(".\\tasks.md"), QStringLiteral("-O"),
                                    QStringLiteral(".\\CLAUDE.md") };
        QCOMPARE(r.nvimForwardArgs, expected);
    }

    void splitsPwshMangledWithDashCAndSpaceyValue() {
        // `qvim -c "set number" foo.txt` arriving from pwsh as one crushed arg.
        FakeArgv av{ { QStringLiteral("qvim"),
                       QStringLiteral("\"-c\" \"set number\" \"foo.txt\"") } };
        const auto r = parseArgv(av.argc(), av.argv());
        const QStringList expected{ QStringLiteral("-c"), QStringLiteral("set number"),
                                    QStringLiteral("foo.txt") };
        QCOMPARE(r.nvimForwardArgs, expected);
    }

    void leavesUnmatchedQuoteAlone() {
        // Single trailing quote — not a wrapping pair, leave as-is so we
        // don't mangle legitimate filenames containing a quote.
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("weird\"name.txt") } };
        const auto r = parseArgv(av.argc(), av.argv());
        QCOMPARE(r.nvimForwardArgs, QStringList{ QStringLiteral("weird\"name.txt") });
    }

    void preservesSingleQuotedPathWithSpace() {
        // Legit case: a path with a space inside, quoted properly so it
        // arrives as `"C:\Program Files\foo.txt"`. Outer quotes strip; no
        // `" "` inside the inner string, so we don't try to split.
        FakeArgv av{ { QStringLiteral("qvim"), QStringLiteral("\"C:\\Program Files\\foo.txt\"") } };
        const auto r = parseArgv(av.argc(), av.argv());
        QCOMPARE(r.nvimForwardArgs, QStringList{ QStringLiteral("C:\\Program Files\\foo.txt") });
    }
};

QTEST_GUILESS_MAIN(TestArgvParser)
#include "test_argv_parser.moc"
