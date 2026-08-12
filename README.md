# qvim

## Overview

qvim is a Neovim GUI client for Windows, written in C++23 / Qt 6.10 / QML. It drives an embedded
`nvim --embed` process and talks to it over msgpack-rpc. The user-facing shell is QML, the text grid
is rendered by a `QQuickPaintedItem`, and keyboard input is translated from Qt key events into nvim
keycodes. It is currently Windows-only (built with the Visual Studio 2026 generator and MSVC).

For building and running, see [`SETUP.md`](SETUP.md). For contributor conventions, the correctness
and performance invariants, and the test tiers, see [`AGENTS.md`](AGENTS.md).

## Architecture

qvim is a thin, reactive layer over Neovim: Neovim owns all editing state and emits a redraw event
stream, and qvim's job is to turn that stream into pixels and to turn user input back into nvim
keycodes.

### Redraw pipeline (nvim → screen)

```
nvim --embed
    │  msgpack-rpc (redraw events)
    ▼
MsgpackRpc          transport: frames msgpack-rpc over the nvim process pipes
    ▼
NvimConnector       decodes and dispatches redraw events (grid_line, grid_scroll,
    │               mode_change, hl_attr_define, default_colors_set, flush, …)
    ├──────────────► GridModel        row-major cell grid (text + hl_id per cell)
    ├──────────────► HighlightTable   hl_attr cache (colours + attrs per hl_id)
    └──────────────► ModeInfo         cursor shape / current mode index
                         │
                         ▼
                     GridItem  (QQuickPaintedItem)   paints cell content
                     CursorItem (QQuickPaintedItem)  paints the cursor overlay
                         │
                         ▼
                     QML shell (qml/: Main, Shell, Tabline, PopupMenu, Cmdline)
```

`NvimConnector` exposes redraw notifications as `Q_SIGNAL`s that the QML shell connects to
directly — there is no imperative pull from QML back into RPC state.

### Input path (keyboard → nvim)

```
Qt key event on GridItem
    ▼
InputHandler::keyToNvim   QKeyEvent → well-bracketed nvim keycode string
    ▼
NvimConnector::input()    forwards the keycode to nvim
    ▼
MsgpackRpc                writes it over msgpack-rpc
    ▼
nvim --embed
```

`GridItem` owns the Qt input event and calls `InputHandler` — QML never invokes `InputHandler`
directly.

### Load-bearing invariants

- The redraw event stream from `nvim --embed` is the single source of truth for screen state.
- `paint()` is a pure function of (`GridModel`, `HighlightTable`, `ModeInfo`, cursor state) and
  never reaches back into RPC state from the paint path.

## Launch performance (Windows cold start)

The first launch of qvim *after a build or update* can take 10-20 seconds, while every subsequent
launch is ~1 second. This is not qvim code — it is **Microsoft Defender's real-time "first-sight"
scan** of `qvim.exe` and the Qt DLLs it loads. Defender caches its verdict by file **content hash**,
so the scan cost is paid once per unique build and then launches are fast — until the next rebuild
produces new bytes and the whole scan happens again.

Two things keep cold start down:

- **The deploy is trimmed to only the Qt modules qvim actually loads.** The post-build step copies an
  explicit list of ~20 runtime DLLs plus the `QtQuick`, `QtQml`, and `QtTest` QML modules that are
  imported, instead of the whole Qt Quick tree (Controls/Dialogs/Particles/Effects, ~150 MB). Fewer
  and smaller files means less for Defender to scan on first sight.

- **A Defender exclusion collapses the cold launch outright** (~8s → ~1.5s in measurement, a 5×
  win). Run this once, from an **elevated** PowerShell, against your build output directory:

  ```pwsh
  pwsh -NoProfile -File scripts\add-defender-exclusion.ps1
  ```

  By default it excludes `build\release\RelWithDebInfo`; pass `-Path <dir>` for a custom install
  location, or `-Remove` to undo. An excluded directory is no longer scanned by real-time
  protection, so only exclude build output you produce and trust — never a broad location like your
  whole user profile or a downloads folder.
