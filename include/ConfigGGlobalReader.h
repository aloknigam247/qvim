#pragma once

#include <QHash>
#include <QString>
#include <QVariant>

namespace qvim {

class Config;
class NvimConnector;

// Reads `g:qvim_<name>` global variables from a running nvim and applies
// them to `cfg` via Config::setFromGGlobal.
//
// Variables that are unset (nvim_get_var returns an error) are silently
// skipped — there is no requirement that any g: be present.
//
// `readFromMap` exists as a testability seam: it applies the same type
// coercion + filtering rules but reads from a pre-built map of QVariants
// instead of a live nvim connection.
class ConfigGGlobalReader {
public:
    static void read(NvimConnector& connector, Config& cfg);
    static void readFromMap(const QHash<QString, QVariant>& vars, Config& cfg);
};

} // namespace qvim
