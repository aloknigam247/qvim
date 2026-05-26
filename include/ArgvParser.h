#pragma once

#include <QStringList>

namespace qvim {

struct QvimArgs {
    QStringList nvimForwardArgs;
    bool helpRequested = false;
    bool versionRequested = false;
    // `nvim -` reads the file content from stdin. With qvim, nvim --embed
    // already owns its own stdin (the RPC pipe), so the `-` token is consumed
    // here: main.cpp reads qvim's stdin and pushes the bytes into the first
    // buffer over RPC after attach.
    bool stdinAsBuffer = false;
};

// Parses qvim's process argv. qvim owns only `--help`/`-h`, `--version`/`-v`,
// and the reserved `--qvim-*` namespace. Everything else (including vim-style
// options like `-O`, `+10`, `-c "set number"`, and bare file paths) is
// forwarded verbatim to the embedded `nvim --embed` after `--embed`.
QvimArgs parseArgv(int argc, char** argv);

} // namespace qvim
