#pragma once

#include <QStringList>

namespace qvim {

struct QvimArgs {
    QStringList nvimForwardArgs;
    bool helpRequested = false;
    bool versionRequested = false;
};

// Parses qvim's process argv. qvim owns only `--help`/`-h`, `--version`/`-v`,
// and the reserved `--qvim-*` namespace. Everything else (including vim-style
// options like `-O`, `+10`, `-c "set number"`, and bare file paths) is
// forwarded verbatim to the embedded `nvim --embed` after `--embed`.
QvimArgs parseArgv(int argc, char** argv);

} // namespace qvim
