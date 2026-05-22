#pragma once

#include <QHash>
#include <QRawFont>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <list>

namespace qvim {

// FontFallback bypasses Qt's text engine substitution for non-ASCII codepoints.
// QPainter::drawText() will silently swap the configured family for a fallback
// when shaping; that fallback often lacks Nerd Font PUA glyphs (U+E000-U+F8FF,
// U+F0000-U+F1FFF) and the cell renders as tofu. We instead resolve each
// codepoint to a concrete QRawFont — either the primary face or a probed
// fallback face that actually carries the glyph — and draw via QGlyphRun.
//
// Lookups are bounded to 4096 entries via a hand-rolled LRU (std::list +
// QHash). NOT QCache, which has eviction semantics tied to a "cost" model
// that doesn't match a simple count cap.
class FontFallback {
public:
    struct Resolved {
        // Concrete face to draw with. Always valid after a successful resolve.
        QRawFont raw;
        // True when the primary face supports the codepoint. Callers can keep
        // the fast batched-drawText path in that case and only fall back to
        // QGlyphRun when this is false.
        bool isPrimary = false;
    };

    FontFallback();

    // Rebuild primary + fallback list when the configured font changes. Primary
    // is the user-selected family/size; fallback list is auto-built from
    // QFontDatabase, filtered for Nerd Font / symbol families, all at the same
    // pixel size so glyph metrics line up with the cell grid.
    void setPrimary(const QString& family, qreal pixelSize);

    // Resolve a codepoint to a face. Hits the LRU first; on miss probes
    // primary, then each fallback, caches and returns. If nothing carries the
    // glyph the primary is returned with isPrimary=true (caller will paint
    // tofu, which is the same behaviour as before this class existed — never
    // worse).
    const Resolved& resolve(char32_t cp);

    // Visible cache size — for tests and diagnostics.
    int cacheSize() const { return static_cast<int>(m_lru.size()); }

    // Cap is 4096 by convention; exposed for tests.
    static constexpr int kCacheCap = 4096;

    // For tests: exposed so a test can inject a deterministic fallback list
    // instead of relying on whatever Nerd Fonts happen to be installed.
    void setFallbackFamiliesForTest(const QStringList& families);

private:
    void rebuildPrimary();
    void rebuildFallbacks();
    void touch(char32_t cp);                  // LRU promote
    void insert(char32_t cp, Resolved r);     // LRU insert + evict if needed

    QString          m_family;
    qreal            m_pixelSize = 12.0;
    QRawFont         m_primary;
    QVector<QRawFont> m_fallbacks;
    QStringList      m_overrideFamilies;      // test hook; empty in production

    // LRU: front = most recently used. The hash maps codepoint to (list_iter,
    // resolved). Iterators into std::list are stable across other inserts /
    // erases, so we can splice in O(1) on every hit.
    struct Entry {
        std::list<char32_t>::iterator it;
        Resolved                       resolved;
    };
    std::list<char32_t>     m_lru;
    QHash<char32_t, Entry>  m_map;
};

} // namespace qvim
