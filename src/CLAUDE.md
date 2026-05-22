# src/ — agent guide (delta)

Root rules in `D:\qvim\CLAUDE.md` still apply. This file adds C++-only specifics.

## File-to-responsibility map

- `main.cpp` — app entry, QQmlApplicationEngine wiring, context properties.
- `AppIcon.{h,cpp}` — `setupApplicationIcon(QGuiApplication&)`; loads `:/icon.ico` via static-lib QRC init.
- `ArgvParser.{h,cpp}` — splits qvim's argv. `--qvim-*` / `--help` / `--version` consumed locally; everything else forwarded to `nvim --embed`. Unmangles pwsh-7's native-arg quoting.
- `MsgpackRpc.{h,cpp}` — nvim transport. Owns the unpacker arena. `msgpack::object` views into this arena.
- `NvimConnector.{h,cpp}` — redraw event dispatch. Hot-path switch over event names.
- `GridModel.{h,cpp}` — row-major cell grid. Single source of truth for what `paint()` reads.
- `HighlightTable.{h,cpp}` — `hl_attr_define` cache, hashed lookup by id.
- `ModeInfo.{h,cpp}` — cursor shape + mode index from `mode_info_set` / `mode_change`.
- `GridItem.{h,cpp}` — `QQuickPaintedItem` renderer. `paint()` must stay pure.
- `InputHandler.{h,cpp}` — `QKeyEvent` → nvim keycodes. Bracket-balanced strings only.
- `CmdlineModel.{h,cpp}` — ext_cmdline state for the QML cmdline overlay.
- `MessagesModel.{h,cpp}` — ext_messages buffer (`QAbstractListModel`).
- `PopupMenuModel.{h,cpp}` — ext_popupmenu state.
- `TablineModel.{h,cpp}` — ext_tabline state (tabs + buffers).
- `ClipboardBridge.{h,cpp}` — implements nvim's clipboard provider via Qt clipboard.
- `FontFallback.{h,cpp}` — per-codepoint primary-face / fallback-face resolver via `QRawFont`. LRU-bounded (4096). Used by GridItem paint slow path for non-ASCII.
- `RecentProjectsModel.{h,cpp}` — non-nvim feature; JSON-backed list under `%APPDATA%\qvim`.
- `ResizeCoalescer.{h,cpp}` — debounces window-resize → `nvim_ui_try_resize` RPCs (~24ms). Owned by `NvimConnector`; GridItem letterboxes between RPCs.
- `WindowChrome.{h,cpp}` — Windows-only `DwmSetWindowAttribute` wrapper. `applyToWindow(QQuickWindow*, QColor)` repaints the OS caption to match nvim's editor bg.

## Conventions

- All public types live in `namespace qvim`.
- Anything QML touches uses `QML_ELEMENT` (creatable) or `QML_UNCREATABLE("Owned by ...")`.
- `Q_PROPERTY` with NOTIFY for every field QML binds to. Never `setContextProperty` a QML_ELEMENT type.
- Pass `msgpack::object` by `const&`. Copying breaks the arena lifetime contract.
- Hot buffers: `QVarLengthArray`, `QString::reserve`, `resize` — not `QList::append` in loops.

## Bug patterns to avoid

- Copying `msgpack::object` (it's a non-owning view; the unpacker owns the bytes).
- Mutating `GridModel` / `HighlightTable` from `GridItem::paint()`. Paint is pure.
- Allocating per cell in the redraw or paint path. Batch text runs by `(hl_id, font_state)`.
- Throttling `update()` manually — Qt already coalesces.
- Reaching back into RPC / `NvimConnector` from `paint()`.

## Build/test cheatsheet

Iterate on a single `.cpp` without re-running vcpkg-configure:
```pwsh
msbuild <WORKTREE>\build\dev\qvim_lib.vcxproj /p:Configuration=Debug /m
```
Full build + tests (worktree):
```pwsh
cmake --build --preset dev
ctest --preset dev --output-on-failure -R test_grid_model
```

## Templates

- Row-major cell storage: `GridModel.cpp`.
- Hashed attr cache: `HighlightTable.cpp`.
- Redraw event dispatch: `NvimConnector.cpp`.
- `QQuickPaintedItem` pure-paint renderer: `GridItem.cpp`.
- `QKeyEvent` → nvim keycode encoding: `InputHandler.cpp`.
