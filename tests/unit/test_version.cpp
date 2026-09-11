#include <QtTest>
#include <QRegularExpression>

#include "Version.h"
#include "VersionString.h"

// Stringify the CMake-injected PROJECT_VERSION token so the header value can be
// checked against an independently supplied expectation (not a sibling macro
// from the same substitution, which would be tautological).
#define QVIM_STR2(x) #x
#define QVIM_STR(x) QVIM_STR2(x)

class TestVersion : public QObject {
    Q_OBJECT
private slots:
    void macroIsSemver() {
        const QString v = QString::fromLatin1(QVIM_VERSION);
        QVERIFY2(!v.isEmpty(), "QVIM_VERSION must not be empty");
        QRegularExpression re(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));
        QVERIFY2(re.match(v).hasMatch(),
                 qPrintable(QStringLiteral("QVIM_VERSION '%1' is not X.Y.Z").arg(v)));
    }

    void macroMatchesProjectVersion() {
        QCOMPARE(QString::fromLatin1(QVIM_VERSION),
                 QString::fromLatin1(QVIM_STR(QVIM_EXPECTED_VERSION)));
    }

    void cliStringIsExact() {
        QCOMPARE(qvim::versionString(),
                 QStringLiteral("qvim ") + QString::fromLatin1(QVIM_VERSION));
    }
};

QTEST_GUILESS_MAIN(TestVersion)
#include "test_version.moc"
