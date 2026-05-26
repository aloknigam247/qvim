# src/ — agent guide (delta)

Root rules in `D:\qvim\CLAUDE.md` still apply. This file adds C++-only specifics.

## File-to-responsibility map

- `main.cpp` — app entry, QQmlApplicationEngine wiring, context properties.
- `AppIcon.{h,cpp}` — `setupApplicationIcon(QGuiApplication&)`; loads `:/icon.ico` via static-lib QRC init.
- `ArgvParser.{h,cpp}` — splits qvim's argv. `--qvim-*` / `--help` / `--version` consumed locally; everything else forwarded to `nvim --embed`. Unmangles pwsh-7's native-arg quoting.
- `Config.{h,cpp}` — typed config registry (Bool/Int/Float/String/StringList). CLI > g: > default precedence; `changed(name)` signal on resolved-value change.
- `ConfigCliReader.{h,cpp}` — extracts `--qvim-<name>=<value>` from argv into Config.
- `ConfigGGlobalReader.{h,cpp}` — populates Config from `g:qvim_<name>` once after attachComplete.
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
- Drawing Private Use Area codepoints (nerd-font icons, powerline glyphs) via `QPainter::drawText`. Qt's text shaper silently drops them on Windows (QTBUG-116417, root cause traced to `QChar::isPrint()` in `qtextengine.cpp`; QTBUG-110502 closed Won't Do). Use the hybrid pattern in `GridItem::paint()`: drawText for ordinary text (preserves Qt's automatic font fallback, which is what makes e.g. emoji render via the system emoji font), plus a per-row `QRawFont::glyphIndexesForString` + `drawGlyphRun` overlay pass for PUA-bearing cells. A `rowHasPua` flag set during run-building gates the overlay so PUA-free rows pay zero overhead.

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
- Rectilinear rounded overlay (union polygon + uniform `quadTo(corner, pOut)` for convex + concave inverse rounding): the visual-selection block in `GridItem.cpp::paint()`.

## Adding a config option

`Config` is the typed registry, but it's inert until something is wired to it. To add `g:qvim_<name>` / `--qvim-<name>` support:

1. **Register** in `main.cpp` right after `qvim::Config cfg;` and BEFORE `ConfigCliReader::extract`:
   ```cpp
   cfg.registerOption(QStringLiteral("<name>"), qvim::ConfigType::<Type>, <default>);
   ```
2. **Wire** the resolved value to the target object. The lambda + signal pattern (used for `rounded_highlights`):
   ```cpp
   auto apply<X> = [&]() {
       if (auto* target = connector.<accessor>()) {
           target-><setter>(cfg.value(QStringLiteral("<name>")).to<Type>());
       }
   };
   apply<X>();
   QObject::connect(&cfg, &qvim::Config::changed, &connector,
                    [apply<X>](const QString& name) {
                        if (name == QStringLiteral("<name>")) apply<X>();
                    });
   ```
3. **Order matters**: the `Config::changed` connection MUST be in place BEFORE the `attachComplete` handler runs `ConfigGGlobalReader::read`, otherwise the user's `g:` value resolves silently and never reaches the target.
4. If the option needs CLI parity, nothing extra is needed — `ConfigCliReader::extract` auto-discovers `--qvim-<name>=<value>` for any registered option.
