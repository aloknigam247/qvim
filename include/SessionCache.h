#pragma once

#include <QString>

namespace qvim {

// Persists the last-known grid dimensions and guifont between sessions so that
// the next launch can issue nvim_ui_attach with the correct (cols, rows)
// immediately — eliminating the placeholder→resize round-trip when the font
// hasn't changed.
//
// Storage: %APPDATA%/qvim/session.json.
struct SessionCache {
    QString guifont;
    int cols = 0;
    int rows = 0;

    // Returns true if all fields are populated (cols > 0, rows > 0).
    bool isValid() const { return cols > 0 && rows > 0; }

    // Load from disk. Returns a default (invalid) instance if the file is
    // missing, corrupt, or has incompatible schema.
    static SessionCache load();

    // Save to disk. Creates the directory if needed. Errors are silently
    // ignored (cache is best-effort).
    static void save(const SessionCache &cache);
};

} // namespace qvim
