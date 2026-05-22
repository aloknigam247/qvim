#include "FontFallback.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>

namespace qvim {

namespace {

// Pixel size is exposed as an explicit setting on QRawFont, in pixels not
// points. We expect callers to pass the same px-size the QFont used to render
// the grid; otherwise the fallback glyph would not match the cell metrics.
QRawFont rawFromFamily(const QString& family, qreal pixelSize) {
    QFont f(family);
    f.setPixelSize(qRound(pixelSize));
    // Ask Qt to give us the exact face we asked for. QRawFont::fromFont still
    // does style/family matching but not the per-glyph substitution that
    // QPainter::drawText does; that's the substitution we're working around.
    return QRawFont::fromFont(f);
}

bool looksLikeNerdOrSymbolFamily(const QString& family) {
    // Match common Nerd Font / symbol families. Case-insensitive substring
    // matches; cheap and good enough — the list is short.
    const QString lc = family.toLower();
    static const char* kHints[] = {
        "nerd",
        "symbols",
        "symbol",
        "powerline",
        "awesome",
        "icons",
        "devicons",
        "octicons",
        "material",
        "codicon",
        "noto color emoji",
        "segoe ui symbol",
        "segoe ui emoji",
    };
    for (const char* h : kHints) {
        if (lc.contains(QLatin1String(h))) return true;
    }
    return false;
}

} // namespace

FontFallback::FontFallback() = default;

void FontFallback::releaseRenderResources() {
    m_primary = QRawFont();
    m_fallbacks.clear();
    m_lru.clear();
    m_map.clear();
    m_lazyBuilt = false;
}

void FontFallback::setPrimary(const QString& family, qreal pixelSize) {
    if (family == m_family && qFuzzyCompare(pixelSize, m_pixelSize) && m_lazyBuilt) {
        return;
    }
    m_family    = family;
    m_pixelSize = pixelSize;
    m_lru.clear();
    m_map.clear();
    // Resolve the fallback family list now (QFontDatabase::families is
    // thread-safe and cheap). QRawFont construction is deferred until the
    // first resolve() call so it happens on the paint thread.
    m_fallbackFamilies.clear();
    if (!m_overrideFamilies.isEmpty()) {
        m_fallbackFamilies = m_overrideFamilies;
    } else {
        for (const QString& fam : QFontDatabase::families()) {
            if (looksLikeNerdOrSymbolFamily(fam) && fam != m_family) {
                m_fallbackFamilies.append(fam);
            }
        }
    }
    m_lazyBuilt = false;
    m_primary = QRawFont();
    m_fallbacks.clear();
}

void FontFallback::setFallbackFamiliesForTest(const QStringList& families) {
    m_overrideFamilies = families;
    m_fallbackFamilies = families;
    m_lru.clear();
    m_map.clear();
    m_lazyBuilt = false;
    m_fallbacks.clear();
}

void FontFallback::rebuildPrimary() {
    m_primary = rawFromFamily(m_family, m_pixelSize);
}

void FontFallback::rebuildFallbacks() {
    m_fallbacks.clear();
    m_fallbacks.reserve(m_fallbackFamilies.size());
    for (const QString& fam : m_fallbackFamilies) {
        QRawFont rf = rawFromFamily(fam, m_pixelSize);
        if (rf.isValid()) m_fallbacks.append(std::move(rf));
    }
}

const FontFallback::Resolved& FontFallback::resolve(char32_t cp) {
    // Lazy build on the calling (paint) thread so QRawFont is owned by that
    // thread — QFontEngine asserts cross-thread use.
    if (!m_lazyBuilt) {
        rebuildPrimary();
        rebuildFallbacks();
        m_lazyBuilt = true;
    }

    auto it = m_map.find(cp);
    if (it != m_map.end()) {
        touch(cp);
        return it.value().resolved;
    }

    Resolved r;
    if (m_primary.isValid() && m_primary.supportsCharacter(cp)) {
        r.raw       = m_primary;
        r.isPrimary = true;
    } else {
        bool found = false;
        for (const QRawFont& f : m_fallbacks) {
            if (f.isValid() && f.supportsCharacter(cp)) {
                r.raw       = f;
                r.isPrimary = false;
                found       = true;
                break;
            }
        }
        if (!found) {
            r.raw       = m_primary;
            r.isPrimary = true; // caller will draw tofu via the normal path
        }
    }

    insert(cp, std::move(r));
    return m_map.find(cp).value().resolved;
}

void FontFallback::touch(char32_t cp) {
    auto it = m_map.find(cp);
    if (it == m_map.end()) return;
    // O(1) move-to-front using splice on the existing iterator.
    m_lru.splice(m_lru.begin(), m_lru, it.value().it);
    it.value().it = m_lru.begin();
}

void FontFallback::insert(char32_t cp, Resolved r) {
    m_lru.push_front(cp);
    m_map.insert(cp, Entry{m_lru.begin(), std::move(r)});

    while (static_cast<int>(m_lru.size()) > kCacheCap) {
        const char32_t victim = m_lru.back();
        m_lru.pop_back();
        m_map.remove(victim);
    }
}

} // namespace qvim
