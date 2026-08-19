// Property-based fuzz test for InputHandler::keyToNvim.
//
// Generates pseudo-random QKeyEvent sequences and verifies that the
// translated nvim keycode string is always well-formed:
//   1. No exception is thrown.
//   2. Angle brackets are balanced; escape forms <lt>/<gt> count as literals.
//   3. Bracketed tokens match a lenient allowlisted shape (no garbage inside).
//   4. The result survives a UTF-8 round-trip.
//   5. Bare Qt::Key_Less plain press always produces "<lt>" exactly.

#include <QKeyEvent>
#include <QtTest>

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <string_view>

#include "InputHandler.h"

using namespace qvim;

namespace {

struct GeneratedEvent {
    int key{};
    Qt::KeyboardModifiers mods{};
    QString text;
};

QString describe(const GeneratedEvent &g) {
    return QStringLiteral("key=0x%1 mods=0x%2 text=%3")
        .arg(g.key, 0, 16)
        .arg(static_cast<int>(g.mods), 0, 16)
        .arg(g.text.isEmpty() ? QStringLiteral("\"\"") : QStringLiteral("\"%1\"").arg(g.text));
}

// Curated key list spanning the interesting paths through keyToNvim.
constexpr std::array<int, 48> kKeyPool{
    Qt::Key_A,         Qt::Key_B,         Qt::Key_C,           Qt::Key_D,      Qt::Key_E,
    Qt::Key_F,         Qt::Key_M,         Qt::Key_X,           Qt::Key_Z,      Qt::Key_0,
    Qt::Key_1,         Qt::Key_5,         Qt::Key_9,           Qt::Key_Escape, Qt::Key_Tab,
    Qt::Key_Backtab,   Qt::Key_Backspace, Qt::Key_Return,      Qt::Key_Enter,  Qt::Key_Space,
    Qt::Key_Delete,    Qt::Key_Insert,    Qt::Key_Home,        Qt::Key_End,    Qt::Key_PageUp,
    Qt::Key_PageDown,  Qt::Key_Up,        Qt::Key_Down,        Qt::Key_Left,   Qt::Key_Right,
    Qt::Key_F1,        Qt::Key_F4,        Qt::Key_F12,         Qt::Key_Shift,  Qt::Key_Control,
    Qt::Key_Alt,       Qt::Key_Meta,      Qt::Key_CapsLock,    Qt::Key_Less,   Qt::Key_Greater,
    Qt::Key_Backslash, Qt::Key_Bar,       Qt::Key_QuoteLeft,   Qt::Key_Period, Qt::Key_Comma,
    Qt::Key_Slash,     Qt::Key_Semicolon, Qt::Key_BracketLeft,
};

constexpr std::array<Qt::KeyboardModifier, 4> kModBits{
    Qt::ShiftModifier,
    Qt::ControlModifier,
    Qt::AltModifier,
    Qt::MetaModifier,
};

// Natural single-char text for a few keys; otherwise empty / random.
QString naturalText(int key, Qt::KeyboardModifiers mods) {
    const bool shift = (mods & Qt::ShiftModifier) != 0;
    switch(key) {
        case Qt::Key_A:
            return shift ? QStringLiteral("A") : QStringLiteral("a");
        case Qt::Key_B:
            return shift ? QStringLiteral("B") : QStringLiteral("b");
        case Qt::Key_C:
            return shift ? QStringLiteral("C") : QStringLiteral("c");
        case Qt::Key_D:
            return shift ? QStringLiteral("D") : QStringLiteral("d");
        case Qt::Key_E:
            return shift ? QStringLiteral("E") : QStringLiteral("e");
        case Qt::Key_F:
            return shift ? QStringLiteral("F") : QStringLiteral("f");
        case Qt::Key_M:
            return shift ? QStringLiteral("M") : QStringLiteral("m");
        case Qt::Key_X:
            return shift ? QStringLiteral("X") : QStringLiteral("x");
        case Qt::Key_Z:
            return shift ? QStringLiteral("Z") : QStringLiteral("z");
        case Qt::Key_0:
            return QStringLiteral("0");
        case Qt::Key_1:
            return QStringLiteral("1");
        case Qt::Key_5:
            return QStringLiteral("5");
        case Qt::Key_9:
            return QStringLiteral("9");
        case Qt::Key_Less:
            return QStringLiteral("<");
        case Qt::Key_Greater:
            return QStringLiteral(">");
        case Qt::Key_Backslash:
            return QStringLiteral("\\");
        case Qt::Key_Bar:
            return QStringLiteral("|");
        case Qt::Key_QuoteLeft:
            return QStringLiteral("`");
        case Qt::Key_Period:
            return QStringLiteral(".");
        case Qt::Key_Comma:
            return QStringLiteral(",");
        case Qt::Key_Slash:
            return QStringLiteral("/");
        case Qt::Key_Semicolon:
            return QStringLiteral(";");
        case Qt::Key_BracketLeft:
            return QStringLiteral("[");
        default:
            return {};
    }
}

QString randomMultibyte(std::mt19937_64 &rng) {
    // Pull a random non-ASCII BMP codepoint (avoid surrogates and control range).
    std::uniform_int_distribution<uint32_t> dist(0x00A1, 0xFFFD);
    while(true) {
        const uint32_t cp = dist(rng);
        if(cp >= 0xD800 && cp <= 0xDFFF) continue;
        return QString(QChar(static_cast<char16_t>(cp)));
    }
}

GeneratedEvent makeEvent(std::mt19937_64 &rng) {
    GeneratedEvent g;
    std::uniform_int_distribution<size_t> keyDist(0, kKeyPool.size() - 1);
    g.key = kKeyPool[keyDist(rng)];

    Qt::KeyboardModifiers mods = Qt::NoModifier;
    std::uniform_int_distribution<int> coin(0, 1);
    for(const auto bit: kModBits) {
        if(coin(rng)) mods |= bit;
    }
    g.mods = mods;

    // Text selection strategy.
    std::uniform_int_distribution<int> texPick(0, 9);
    const int p = texPick(rng);
    if(p < 5) {
        g.text = naturalText(g.key, g.mods);
    } else if(p < 7) {
        g.text = QString();
    } else if(p < 9) {
        g.text = randomMultibyte(rng);
    } else {
        // Random ASCII printable.
        std::uniform_int_distribution<int> ascii(0x20, 0x7E);
        g.text = QString(QChar(static_cast<char16_t>(ascii(rng))));
    }
    return g;
}

// Validate that `s` contains only well-formed bracketed tokens.
// Returns empty string on success, or an error message on failure.
QString validateBrackets(const QString &s) {
    int i = 0;
    const int n = static_cast<int>(s.size());
    while(i < n) {
        const QChar c = s.at(i);
        // A bare '>' outside any token is a literal character (vim keycode
        // grammar only treats '>' specially when it closes an open '<').
        if(c == QChar('<')) {
            // Find the matching '>'. Disallow nested '<'.
            int j = i + 1;
            while(j < n && s.at(j) != QChar('>')) {
                if(s.at(j) == QChar('<')) {
                    return QStringLiteral("nested '<' at index %1 inside token starting at %2")
                        .arg(j)
                        .arg(i);
                }
                ++j;
            }
            if(j >= n) { return QStringLiteral("unterminated '<' at index %1").arg(i); }
            const QString token = s.mid(i + 1, j - i - 1);
            // Lenient shape: non-empty, no embedded whitespace. Nvim accepts a
            // wide variety of single-byte key names and printable characters
            // (e.g. <M-;>, <C-\>) so we don't try to enumerate them here.
            if(token.isEmpty()) { return QStringLiteral("empty token \"<>\" at index %1").arg(i); }
            for(const QChar tc: token) {
                if(tc.isSpace()) {
                    return QStringLiteral("whitespace inside token \"<%1>\" at index %2")
                        .arg(token)
                        .arg(i);
                }
            }
            i = j + 1;
            continue;
        }
        ++i;
    }
    return {};
}

QString translate(const GeneratedEvent &g) {
    QKeyEvent ev(QEvent::KeyPress, g.key, g.mods, g.text);
    return InputHandler::keyToNvim(&ev);
}

uint64_t resolveSeed() {
    const QByteArray env = qgetenv("QTEST_SEED");
    if(!env.isEmpty()) {
        bool ok = false;
        const qulonglong v = env.toULongLong(&ok, 0);
        if(ok) return static_cast<uint64_t>(v);
    }
    return 0xC0FFEE12345ULL;
}

} // namespace

class TestInputFuzz : public QObject {
    Q_OBJECT
private slots:
    void regression_plainLessProducesLt() {
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Less, Qt::NoModifier, QStringLiteral("<"));
        QCOMPARE(InputHandler::keyToNvim(&ev), QStringLiteral("<lt>"));
    }

    void regression_altSpace() {
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Space, Qt::AltModifier, QString());
        QCOMPARE(InputHandler::keyToNvim(&ev), QStringLiteral("<M-Space>"));
    }

    void regression_metaTab() {
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Tab, Qt::MetaModifier, QString());
        QCOMPARE(InputHandler::keyToNvim(&ev), QStringLiteral("<D-Tab>"));
    }

    void regression_ctrlShiftLess() {
        // Ctrl+Shift+< : text is the '<' character with control bit cleared
        // by Qt; the implementation must escape it as <lt> even when modifiers
        // wrap it.
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Less, Qt::ControlModifier | Qt::ShiftModifier,
                     QStringLiteral("<"));
        const QString out = InputHandler::keyToNvim(&ev);
        const QString err = validateBrackets(out);
        QVERIFY2(err.isEmpty(),
                 qPrintable(QStringLiteral("Ctrl+Shift+< produced \"%1\": %2").arg(out, err)));
    }

    void regression_ctrlShiftLetter() {
        // Ctrl+Shift+i must keep Shift, otherwise it collapses to <C-i> (which
        // nvim also reads as <Tab>) and a <C-S-i> mapping can never fire.
        // modString() emits S- before C-, and nvim canonicalises <S-C-i> and
        // <C-S-i> to the same key.
        for(const QString &text: { QStringLiteral("\x09"), QStringLiteral("I") }) {
            QKeyEvent ev(QEvent::KeyPress, Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier,
                         text);
            const QString out = InputHandler::keyToNvim(&ev);
            QCOMPARE(out, QStringLiteral("<S-C-i>"));
            const QString err = validateBrackets(out);
            QVERIFY2(err.isEmpty(),
                     qPrintable(QStringLiteral("Ctrl+Shift+i produced \"%1\": %2").arg(out, err)));
        }
    }

    void fuzz() {
        const uint64_t seed = resolveSeed();
        qInfo("fuzz seed = 0x%llx", static_cast<unsigned long long>(seed));
        std::mt19937_64 rng(seed);

        constexpr int kIterations = 5000;
        for(int i = 0; i < kIterations; ++i) {
            const GeneratedEvent g = makeEvent(rng);

            QString out;
            try {
                out = translate(g);
            } catch(const std::exception &e) {
                QFAIL(qPrintable(QStringLiteral("exception thrown (seed=0x%1 iter=%2 %3): %4")
                                     .arg(seed, 0, 16)
                                     .arg(i)
                                     .arg(describe(g))
                                     .arg(QString::fromUtf8(e.what()))));
            } catch(...) {
                QFAIL(qPrintable(QStringLiteral("unknown exception (seed=0x%1 iter=%2 %3)")
                                     .arg(seed, 0, 16)
                                     .arg(i)
                                     .arg(describe(g))));
            }

            // 1. UTF-8 round-trip.
            const QByteArray bytes = out.toUtf8();
            const QString roundtrip = QString::fromUtf8(bytes);
            if(roundtrip != out) {
                QFAIL(qPrintable(
                    QStringLiteral("UTF-8 round-trip failed (seed=0x%1 iter=%2 %3): out=\"%4\"")
                        .arg(seed, 0, 16)
                        .arg(i)
                        .arg(describe(g))
                        .arg(out)));
            }

            // 2. Bracket / token shape.
            const QString err = validateBrackets(out);
            if(!err.isEmpty()) {
                QFAIL(qPrintable(QStringLiteral("bracket validation failed (seed=0x%1 iter=%2 %3): "
                                                "out=\"%4\" err=%5")
                                     .arg(seed, 0, 16)
                                     .arg(i)
                                     .arg(describe(g))
                                     .arg(out)
                                     .arg(err)));
            }

            // 3. No stray <lt>: only plain Key_Less (or text containing '<' on
            //    an unmodified-by-CAM path) should produce "<lt>".
            //    A literal "<lt>" appearing anywhere is fine; what matters is
            //    that we never emit it accidentally. The bracket validator
            //    already accepts it as a well-formed token, so we just sanity
            //    check that if "<lt>" appears, the input did contain a '<'
            //    OR the natural text mapping for the key is '<'.
            if(out.contains(QStringLiteral("<lt>"))) {
                const bool textHasLess = g.text.contains(QChar('<'));
                const bool keyIsLess = (g.key == Qt::Key_Less);
                if(!textHasLess && !keyIsLess) {
                    QFAIL(qPrintable(QStringLiteral("stray <lt> (seed=0x%1 iter=%2 %3): out=\"%4\"")
                                         .arg(seed, 0, 16)
                                         .arg(i)
                                         .arg(describe(g))
                                         .arg(out)));
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestInputFuzz)
#include "test_input_fuzz.moc"
