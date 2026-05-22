# qvim — agent guide

qvim is a Neovim GUI client written in C++23 / Qt 6.10 / QML, talking to an embedded `nvim --embed` process over msgpack-rpc. The shell is QML, the grid renderer is a `QQuickPaintedItem`, and tests run against a real `nvim` binary through the offscreen QPA.

## Top priorities

**Correctness and performance are the utmost priorities — over readability, terseness, or stylistic preferences.** When the two are in tension, correctness wins; when correctness is settled, the implementation must not regress per-frame redraw cost.

### Correctness

- The redraw event stream from `nvim --embed` is the single source of truth. Never paper over an unexpected event shape — fix the parser. Silent fallbacks (`if (a.size < N) return;`) are acceptable for forward-compat with new event variants but must not mask bugs in events we already claim to handle.
- `grid_line`, `grid_scroll`, `mode_change`, `hl_attr_define`, `default_colors_set`, and `flush` are hot-path correctness gates. Cover any change here with a unit test under `tests/unit/` or `tests/integration/` before merging.
- `paint()` must be a pure function of (`GridModel`, `HighlightTable`, `ModeInfo`, cursor state). Never reach back into RPC state from the paint path.
- Input encoding (`InputHandler::keyToNvim`) is correctness-critical. Every key path must produce a well-bracketed nvim keycode — no `<lt>` leaks, no unbalanced `<>`, no raw control bytes.

### Performance

- The redraw → paint loop is the hot path. Treat every allocation per cell, per row, per frame as suspect.
- Prefer batching to per-cell work: build text runs by `(hl_id, font_state)` rather than calling `drawText` per cell.
- Use `QVarLengthArray` / `QString::reserve` / explicit `resize` on hot buffers. Avoid `QList` push_back loops in the redraw path.
- Never copy a `msgpack::object` — it's a non-owning view over the unpacker's arena. Pass by const reference.
- `QQuickPaintedItem::update()` is coalesced by Qt; calling it from multiple slots in the same event loop tick is fine. Do not throttle redraw manually unless profiling shows it pays.
- When a feature requires changing the paint cost asymptotic, justify it: an O(rows × cols) loop is fine, an O(rows × cols × glyphs) loop needs evidence it's still under frame budget at 200×60.

### Verify, don't assume

- For changes to the paint path, font handling, or redraw dispatch: run the integration tests (`ctest --preset dev -R 'attach_and_render|resize|insert_and_quit'`) and confirm they still pass.
- For changes to `InputHandler`: `test_input_handler` must pass, and prefer adding a fuzz case if you touched key encoding.
- For changes that affect QML: `test_qml` must pass.
- Don't claim "should work" — run the test.

## Layout

```
include/                    # all project headers, flat, included as "Foo.h"
  MsgpackRpc.h              # nvim transport
  NvimConnector.h           # redraw event dispatch
  GridModel.h               # cell grid (row-major)
  HighlightTable.h          # hl_attr cache
  ModeInfo.h                # cursor shape / mode index
  GridItem.h                # QQuickPaintedItem renderer
  InputHandler.h            # QKeyEvent → nvim keycodes
  TablineModel.h / PopupMenuModel.h / CmdlineModel.h / ...
src/                        # implementation .cpp files
qml/                        # UI shell (Main, Shell, Tabline, PopupMenu, Cmdline)
tests/
  unit/                     # tier 1, headless
  integration/              # tier 2, real nvim, offscreen QPA
  qml/                      # tier 3, qmltest
```

## Build

```pwsh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The post-build step in `CMakeLists.txt` deploys Qt DLLs + plugins next to `qvim.exe` because vcpkg's debug prefix is incompatible with `windeployqt`. Release deployment uses `qt_generate_deploy_app_script` (see CMakeLists).

## Conventions

- C++23, `/W4 /permissive-`. Treat warnings as bugs.
- `Q_PROPERTY` for anything QML touches; never bypass with `setContextProperty` once a type is QML_ELEMENT.
- Notifications from `NvimConnector` are `Q_SIGNAL`s — QML connects directly. No imperative pull from QML.
- Tests use `QSignalSpy` for redraw assertions and `dumpAscii()` for grid snapshots.
- **Reactive QObject-proxy for Repeater delegates.** When a `QHash<id, X>` backs Repeater delegates and the delegate needs to react to field changes on `X`, expose a `Q_INVOKABLE QObject* xFor(id)` returning a proxy whose fields are `Q_PROPERTY` with NOTIFY signals. Bindings re-evaluate in-place; the delegate is never destroyed (preserves focus, glyph cache, blink phase). **Never** use the `model = null; model = list` Repeater-rebuild antipattern — it nukes activeFocusItem and per-instance caches on every nvim window event.
- `Component.onCompleted: forceActiveFocus()` only fires once. To survive later layout reflows, focus must be owned by an item that is itself never destroyed (e.g. the long-lived `baseGrid`, not a Repeater delegate).

## Testing

Three tiers (see `tests/CMakeLists.txt`):
- **Tier 1** `tests/unit/` — headless, no nvim, `QTEST_GUILESS_MAIN`.
- **Tier 2** `tests/integration/` — real `nvim --embed`, `QT_QPA_PLATFORM=minimal`, drives via `NvimConnector` API or QML scene.
- **Tier 3** `tests/qml/` — `qmltest` `TestCase {}` against QML in isolation.

**User-POV smoke harness** (`tests/integration/test_user_smoke.cpp` is the template):
1. `QQmlApplicationEngine.loadFromModule("Qvim", "Main")` after `setContextProperty("$connector", ...)`.
2. Drive input via `QTest::keyClick(window, key)` — routes through Qt's `activeFocusItem` lookup, so a focus regression makes the assertion fail. Never call `conn.input()` directly from a smoke test; that bypasses the bug class smoke tests are meant to catch.
3. Wait on async nvim state with a `waitUntil(pred, timeoutMs)` helper that calls `QCoreApplication::processEvents` between checks.
4. **Visual confirmation**: `QQuickItem::grabToImage()` on the subtree, then assert >1 distinct pixel colour. Catches black-on-black / opacity-0 / z-order render bugs. **Do not** use `QQuickWindow::grabWindow()` — returns null under `minimal` QPA.
5. Set `QQuickWindow::setGraphicsApi(QSGRendererInterface::Software)` in `initTestCase()` so the software renderer is selected before any window is created.
6. Test target must link `qvim_libplugin` + `Qt6::Quick` to register the Qvim QML module statically.

## Gotchas

- vcpkg's Qt deploys only `minimal`, `windows`, `direct2d` QPA plugins — no `offscreen`. Smoke tests use `minimal` + software renderer.
- `ext_messages: true` causes nvim to relocate the message line off-grid, shrinking the active grid by 1 row. Tests that assert exact `rows()` against the attach size must subtract 1.
- Concurrent builds against `D:\qvim\build\dev` contend on `vcpkg-running.lock` and `qvim_lib.pdb`. Serialise builds when running multiple agents, or build individual `.vcxproj` targets via MSBuild to skip the vcpkg-configure step.
- `nvim_ui_try_resize` called from `geometryChange` will fire during cmdline_show reflow. With ext_multigrid this triggers `grid_resize` + `win_pos` for every grid — make sure these don't destroy QML delegates.

## Fanning out subagents

For multi-task batches: group by file ownership.
- Disjoint-file work runs in the main checkout in parallel.
- Hot files (`GridItem.cpp`, `NvimConnector.cpp`, `GridModel.cpp`) — use `isolation: "worktree"` so each agent works on a separate branch. Merge manually afterward.
- Touching `tests/CMakeLists.txt` from many agents is usually safe (alphabetical inserts) but verify after.
