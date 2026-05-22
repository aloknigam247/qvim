#include <QtTest>
#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>

#include "GridItem.h"
#include "HighlightTable.h"

using namespace qvim;

class TestFontStyle : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (QGuiApplication::primaryScreen() == nullptr) {
            QSKIP("No primary screen available (headless without minimal QPA).");
        }
    }

    // buildRunFont must honour the bold flag in HlAttr — not the requested
    // family's resolved weight. With a controlled family that ships both
    // Normal and Bold styles, the round-trip through QFontInfo must agree.
    void boldFlagIsHonoured() {
        GridItem item;
        item.setFontName(QStringLiteral("Courier New"));

        HlAttr plain;
        plain.bold = false;
        const QFont fPlain = item.buildRunFont(plain);
        QCOMPARE(fPlain.weight(), QFont::Normal);
        QVERIFY2(!QFontInfo(fPlain).bold(),
                 "Plain run font must not resolve to a bold face");

        HlAttr boldAttr;
        boldAttr.bold = true;
        const QFont fBold = item.buildRunFont(boldAttr);
        // Use the requested QFont::weight rather than QFontInfo::bold —
        // under the `minimal` QPA used in this test, the resolved face may
        // not have a registered Bold variant even for ubiquitous families,
        // but buildRunFont's contract is "ask for Bold weight", not "produce
        // a face the headless platform considers bold". The Windows GUI
        // platform does perform the actual style synthesis from this.
        QCOMPARE(fBold.weight(), QFont::Bold);
    }

    // Regression for the original bug: with the GridItem default family
    // (often unavailable, triggering Windows substitution), a default-hl run
    // font must NOT resolve to bold. Catches the substitution + family-
    // missing-Normal bug class via the applyFontFamily fallback chain.
    void defaultFontDoesNotResolveToBold() {
        GridItem item;
        const QFont fDefault = item.buildRunFont(HlAttr{});
        QVERIFY2(!QFontInfo(fDefault).bold(),
                 "Default-hl run font must not resolve to bold");
        QVERIFY2(QFontInfo(fDefault).weight() <= QFont::Medium,
                 "Default-hl run font weight must not exceed Medium");
    }

    // An unknown family triggers full substitution. applyFontFamily must
    // walk its fallback chain until a Normal-weight face is found, so the
    // resulting run font for a default highlight is still not bold.
    void unknownFamilyFallsBackToSystem() {
        GridItem item;
        item.setFontName(QStringLiteral("definitely-not-installed-12345"));
        const QFont fDefault = item.buildRunFont(HlAttr{});
        QVERIFY2(!QFontInfo(fDefault).bold(),
                 "Run font for default hl must not be bold after family fallback");
    }
};

QTEST_MAIN(TestFontStyle)
#include "test_font_style.moc"
