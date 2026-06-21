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
- `GridModel.{h,cpp}` — cell grid; single source of truth for what `paint()` reads. Storage is `QVector<QVector<Cell>>` (row-of-rows, NOT flat `QVector<Cell>`) so `scroll(...)` rotates row handles in O(rows) instead of cell-copying in O(rows × cols). Holding `j`/`k` on a 200×60 grid relies on this — flat storage measured ~1000µs/scroll vs ~5µs for the row-handle rotation. Don't regress. Also owns a per-surface `dirty` flag set by content-changing ops (applyLine/scroll/clear/resize) and exposed as `takeDirty(gridId)` (consume + clear) for `GridItem::onFlush` and `isDirty(gridId)` (peek only) for `CursorItem::onCursorActivity`. The peek/take split is intentional — both items see the same flush boundary, only one clears.
- `HighlightTable.{h,cpp}` — `hl_attr_define` cache, hashed lookup by id.
- `ModeInfo.{h,cpp}` — cursor shape + mode index from `mode_info_set` / `mode_change`.
- `GridItem.{h,cpp}` — `QQuickPaintedItem` renderer for **cell content only**. Cursor lives on `CursorItem`. `paint()` must stay pure. `onFlush()` short-circuits via `GridModel::takeDirty(gridId)` — pure cursor moves (grid_cursor_goto + flush, no grid_line) leave the grid clean and `update()` is skipped entirely. This is what keeps held j/k cheap at fullscreen.
- `CursorItem.{h,cpp}` — `QQuickPaintedItem` cursor overlay, sibling of GridItem in `Shell.qml` (z=99, above all grids, below PopupMenu z=100). Owns its own `CursorBlinkState` + blink `QTimer` + 80ms OutExpo `QVariantAnimation` for cursor-move easing. `paint()` draws at `m_animatedPos` (the interpolated pixel position) with the TARGET cell's glyph in defaultBg — the cursor "carries" its destination glyph as it slides. Snaps (no animation) when the grid is dirty in the same flush batch (scroll/paste), so eased motion never fights moving text. Falls back to the target cell on the first paint before any `cursorChanged` has been seen — necessary because nvim's attach-time `grid_cursor_goto` fires before CursorItem is constructed via QML.
- `InputHandler.{h,cpp}` — `QKeyEvent` → nvim keycodes. Bracket-balanced strings only.
- `CmdlineModel.{h,cpp}` — ext_cmdline state for the QML cmdline overlay.
- `MessagesModel.{h,cpp}` — ext_messages buffer (`QAbstractListModel`).
- `PopupMenuModel.{h,cpp}` — ext_popupmenu state.
- `TablineModel.{h,cpp}` — ext_tabline state (tabs + buffers).
- `ClipboardBridge.{h,cpp}` — implements nvim's clipboard provider via Qt clipboard.
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
- `std::freopen(nullptr, "rb", stdin)` to set stdin binary on Windows — triggers /W4 C4996. Use `_setmode(_fileno(stdin), _O_BINARY)` from `<io.h>`/`<fcntl.h>` instead. CRT-initialised `stdin` works fine for /SUBSYSTEM:WINDOWS apps when the parent shell redirects it.
- Drawing Private Use Area codepoints (nerd-font icons, powerline glyphs) via `QPainter::drawText`. Qt's text shaper silently drops them on Windows (QTBUG-116417, root cause traced to `QChar::isPrint()` in `qtextengine.cpp`; QTBUG-110502 closed Won't Do). Use the hybrid pattern in `GridItem::paint()`: drawText for ordinary text (preserves Qt's automatic font fallback, which is what makes e.g. emoji render via the system emoji font), plus a per-row `QRawFont::glyphIndexesForString` + `drawGlyphRun` overlay pass for PUA-bearing cells. A `rowHasPua` flag set during run-building gates the overlay so PUA-free rows pay zero overhead.
- Naming a `Q_PROPERTY` after a member that QQuickItem declares `final`. MOC emits "Final member X is overridden. The override won't be used." and QML treats the property as read-only — so a binding like `cursorItem.baseline: baseGrid.baseline` fails with "Invalid property assignment: baseline is a read-only property". `baseline` was the historical victim here; we renamed to `cellBaseline`. Prefix domain-specific names (`cellWidth`, `cellHeight`, `cellBaseline`) rather than reusing Qt-flavoured ones.

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

- Row-of-rows cell storage with O(rows) `std::rotate`-based scroll: `GridModel.cpp`. Keeps held-key scroll latency in the µs range.
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

## Intercepting an nvim CLI arg

Some nvim CLI args conflict with `--embed` (which owns stdin/stdout for the RPC channel) or only make sense to qvim. Examples: `-` (read stdin into buffer), `+<lnum>` (cursor position — works native, but a hypothetical qvim-specific override would follow the same recipe). To consume an arg locally and simulate its effect via RPC:

1. **Add a flag** to `QvimArgs` in `include/ArgvParser.h` (`bool <feature> = false;`).
2. **Intercept** in `parseArgv` (`src/ArgvParser.cpp`) BEFORE the `out.nvimForwardArgs << arg;` fallback — set the flag and `continue`, do NOT append. This is what keeps the arg out of nvim's argv.
3. **Add an RPC method** on `NvimConnector` that performs the equivalent action via `m_rpc->request(...)` (e.g. `nvim_buf_set_lines`, `nvim_command`, `nvim_input`). Pack the msgpack array yourself in the lambda — match the existing `paste()` / `command()` style.
4. **Wire on `attachComplete`** in `main.cpp` — the nvim instance is fully initialised and the UI is attached, so RPC effects are visible immediately. Capture any payload (e.g. the slurped stdin bytes) by value into the lambda so it survives past the local scope.
5. If the action needs pre-Qt work (e.g. reading qvim's own stdin), do it synchronously between the help/version short-circuits and the `QGuiApplication app(argc, argv);` construction.
