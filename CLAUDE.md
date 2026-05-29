# qvim — agent guide

qvim is a Neovim GUI client written in C++23 / Qt 6.10 / QML, talking to an embedded `nvim --embed` process over msgpack-rpc. The shell is QML, the grid renderer is a `QQuickPaintedItem`, and tests run against a real `nvim` binary through the offscreen QPA.

## Top priorities

**Correctness and performance are the utmost priorities — over readability, terseness, or stylistic preferences.** When the two are in tension, correctness wins; when correctness is settled, the implementation must not regress per-frame redraw cost.

### Correctness

- The redraw event stream from `nvim --embed` is the single source of truth. Never paper over an unexpected event shape — fix the parser. Silent fallbacks (`if (a.size < N) return;`) are acceptable for forward-compat with new event variants but must not mask bugs in events we already claim to handle.
- `grid_line`, `grid_scroll`, `mode_change`, `hl_attr_define`, `default_colors_set`, and `flush` are hot-path correctness gates. Cover any change here with a unit test under `tests/unit/` or `tests/integration/` before merging.
- `ext_hlstate` is opted in at `nvim_ui_attach`, so `hl_attr_define` is `[id, rgb_attr, cterm_attr, info]` (4 elements, not 3). `info` is an array of dicts naming the composing highlight groups (`ui_name`/`hi_name`); `HighlightTable` parses it to flag attrs against the `rounded_highlights` config set. Any new reader of this event must handle the 4-element shape.
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
- **Treat agent reports as claims, not proof.** A single green ctest run can mask: (a) flaky tests that pass intermittently — rerun any new tier-2 test 5–10× before trusting it; (b) tests using `qWarning` / soft thresholds instead of `QVERIFY` for the actual feature signal — read the test source and confirm the strong assertion is a hard fail; (c) pixel-band heuristics that respond to kerning or sub-pixel changes without proving the feature works — for visual features, capture the rendered window (`scripts/screenshot-qvim.ps1`) and inspect.

## Layout

```
include/                    # all project headers, flat, included as "Foo.h"
  MsgpackRpc.h              # nvim transport
  NvimConnector.h           # redraw event dispatch
  GridModel.h               # cell grid (row-major)
  HighlightTable.h          # hl_attr cache
  ModeInfo.h                # cursor shape / mode index
  GridItem.h                # QQuickPaintedItem renderer (cell content only)
  CursorItem.h              # QQuickPaintedItem cursor overlay (sibling of grid)
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

The post-build step in `CMakeLists.txt` deploys Qt DLLs + `qt.conf` + platform plugins + QML modules next to `qvim.exe` for BOTH Debug and Release builds. The block is parameterised on `CMAKE_BUILD_TYPE` (debug DLLs use the `d` suffix, debug vcpkg prefix has the extra `debug/` segment). `qt_generate_deploy_app_script` + `install(SCRIPT ...)` is still wired up for non-Debug consumers running `cmake --install` — but the build-tree exe at `build/<preset>/<Config>/qvim.exe` is launchable directly without an install step.

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
- `QQuickPaintedItem::paint()` runs on the SceneGraph **render thread**, not the GUI thread. Any `QRawFont` / `QFontEngine`-backed object must be constructed AND destroyed on the render thread (the dtor also asserts thread affinity). If you cache one, hook `QQuickWindow::sceneGraphInvalidated` with `Qt::DirectConnection` to release it before the SceneGraph tears down.
- After merging library-only `.cpp` changes into main, the incremental linker may skip relinking `qvim.exe` — the .lib changes, the .exe doesn't. Always force-rebuild with `cmake --build --preset dev --target qvim` before asking the user to verify a fix; otherwise they're testing a stale binary. Same trap exists for an agent's worktree exe: ctest can pass against the new .lib while `<worktree>/build/dev/Debug/qvim.exe` is still from an earlier build. Before validating an agent's worktree binary, check `ls -l <worktree>/build/dev/Debug/qvim.exe` against `git log -1 --format=%ci <branch>` — if the exe is older, delete it and rerun `cmake --build --preset dev --target qvim`.
- `Q_PROPERTY(qreal baseline ...)` on a `QQuickPaintedItem` subclass silently fails: Qt's MOC emits "Final member baseline is overridden. The override won't be used." and QML treats the property as read-only (bindings like `cursorItem.baseline: baseGrid.baseline` produce "Invalid property assignment: baseline is a read-only property"). The trap is that the warning is on the property *override*, not the original — QQuickItem has an internal FINAL `baseline` member that shadows any subclass property of the same name. Rename to anything else (`cellBaseline`, `textBaseline`) to avoid the clash. Other Qt-reserved-name candidates that should not be Q_PROPERTYs on QQuickItem subclasses include `baselineOffset` (real Q_PROPERTY), `clip`, `enabled`, `focus`, `visible`, etc. — when in doubt, prefix with a domain word.
- Bash tool's working directory persists across calls, including when an earlier call `cd`d into a worktree. Cherry-picking and other parent operations must use `git -C /d/qvim ...` or `cd /d/qvim && ...` explicitly — otherwise you commit/build/test in the wrong tree.
- Boot-profile instrumentation is gated behind `QVIM_BOOT_PROFILE=1`. Phase timings go to `qDebug` (invisible under `/SUBSYSTEM:WINDOWS` launched from Explorer) **and** to the path in `QVIM_BOOT_PROFILE_FILE` when set. Use the file form for windowed launches: `$env:QVIM_BOOT_PROFILE=1; $env:QVIM_BOOT_PROFILE_FILE='D:\qvim\_boot.log'; qvim.exe`.
- Qt 6.10 on Windows: `QFont::setFeature(QFont::Tag("liga"), 1)` / `setFeature("calt", 1)` does NOT enable OpenType ligatures for monospace fonts even though the API exists. Cascadia Code / JetBrainsMono ligatures that render in VS Code and Windows Terminal will NOT form in qvim through this path. A working ligature implementation needs a different approach (custom shaping via `QRawFont` + HarfBuzz directly, or `QTextLayout` with explicit feature config) — and a test that asserts on actual ligature *glyph substitution*, not pixel-band density (which responds to kerning changes alone, producing false positives).
- PowerShell's `Set-Content` writes CRLF on Windows. nvim's `-u init.vim` reads CRLF as a `^M` literal at the end of each line, so commands like `autocmd VimEnter * startinsert` become `startinsert^M` and fail with `E488: Trailing characters`. When generating init.vim from a script (e.g. `scripts/screenshot-qvim.ps1` test fixtures), use `[IO.File]::WriteAllText($path, "set ...`n...")` instead — backtick-n in a double-quoted PowerShell string is a single LF.

## Fanning out subagents

For multi-task batches: group by file ownership.
- Disjoint-file work runs in the main checkout in parallel.
- Hot files (`GridItem.cpp`, `NvimConnector.cpp`, `GridModel.cpp`, `CMakeLists.txt`, `src/main.cpp`) — use `isolation: "worktree"` so each agent works on a separate branch. Merge manually afterward.
- Touching `tests/CMakeLists.txt` from many agents is usually safe (alphabetical inserts) but verify after.

### Agent merge protocol (parent serializes, never agents)

When fanning out a batch that touches any shared file:

1. Agents commit to their worktree branch and **STOP** — no self-merge. Self-merging racing on shared files (CMakeLists, main.cpp, tests/CMakeLists.txt) reliably loses commits via `reset --hard $prevHead` on build-failure paths.
2. The parent (you) cherry-picks branches into main sequentially. Use `git cherry-pick <sha>`, not rebase — rebase replays through every intermediate commit and conflicts on each one; cherry-pick re-applies just the target's delta against current main.
3. After each merge: `cmake --build --preset dev` to verify, then force-rebuild qvim if only the .lib changed (see Gotchas).
4. Delete the worktree + branch only AFTER the user verifies the merged behaviour. A test-passing branch can still produce a visually broken binary; keep the worktree until the manual smoke confirms.
5. If a cherry-picked commit must be backed out, prefer `git rebase --onto <good-base> <bad-commit> main` to drop it cleanly from history instead of `git revert`. Revert commits clutter the log and leave the original commit + its revert both visible. Use revert only when the bad commit has been pushed and others may have based work on it.

### Worktree path discipline (agents)

Agents launched with `isolation: "worktree"` get a worktree at `D:\qvim\.claude\worktrees\agent-<id>` but **do not get path translation**. The Edit/Write tools take absolute paths verbatim.

- **Never** use `D:\qvim\` in agent Edit/Write/Bash calls — those writes go to main, bypass isolation, and stomp other agents.
- Always derive paths from `git rev-parse --show-toplevel` or `$PWD`. The agent preamble (`.claude/agent-preamble.md`) states this; reuse that preamble for any new fanout batch.

## Visual validation

For paint-path / font / cursor / selection changes, the authoritative validation is a screenshot of the rendered qvim window (tests can pass while the user-visible feature is broken — see "Verify, don't assume" above).

```pwsh
pwsh scripts/screenshot-qvim.ps1 -File D:\path\to\test.txt -InitFile D:\path\to\init.vim -OutPath D:\out.png
```

Then `Read` the PNG and inspect. Notes on the capture pipeline:
- Qt Quick's SceneGraph doesn't honor `WM_PRINTCLIENT`, so naive `PrintWindow` returns a black client area. The script uses `PrintWindow` with `PW_RENDERFULLCONTENT (0x2)`, which captures DirectComposition / hardware surfaces.
- Windows blocks `SetForegroundWindow` from non-foreground processes; the script does its best with `ShowWindow(SW_RESTORE)` + `SetForegroundWindow`, but if the captured image shows another window's content, the qvim window wasn't actually foregrounded — bump `-SettleMs` or launch into a non-overlapping rect.
- Pass `-u <init.vim>` (not `-c "set ..."`) for guifont with spaces — `-c` args get re-tokenised by nvim and `Cascadia\ Code` gets mis-split.
