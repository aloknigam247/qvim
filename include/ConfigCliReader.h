#pragma once

#include <QStringList>

namespace qvim {

class Config;

// Extracts `--qvim-<name>[=<value>]` arguments from `args`.
//
// Side effects:
//   * For every recognised option (registered in `cfg` with a matching type),
//     the value is parsed and applied via Config::setFromCli.
//   * The corresponding entry is removed from `args` so the remainder can be
//     forwarded to nvim verbatim.
//
// Forms accepted:
//   --qvim-name=value   — value parsed per registered ConfigType
//   --qvim-name         — Bool only; equivalent to value=true
//
// Bool values accept: 1/0, true/false, yes/no, on/off (case-insensitive).
// StringList values are comma-separated.
//
// Unknown names are logged via qDebug and left in `args` so nvim can complain
// if they were not intended for qvim.
class ConfigCliReader {
public:
    static QStringList extract(QStringList& args, Config& cfg);
};

} // namespace qvim
