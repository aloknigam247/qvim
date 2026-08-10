# tests/ — agent guide (delta)

Root `AGENTS.md` covers the three-tier philosophy. This file is operational.

## Tiers

- **Tier 1 — `tests/unit/`** — headless, `QTEST_GUILESS_MAIN`. No nvim.
  Targets: `test_cmdline_pos`, `test_cursor_blink`, `test_cursor_item`, `test_grid_model`, `test_hidpi_rendering`, `test_highlight_table`, `test_input_fuzz`, `test_input_handler`, `test_msgpack_rpc`, `test_popupmenu_select`.
- **Tier 2 — `tests/integration/`** — real `nvim --embed`, `QT_QPA_PLATFORM=minimal`.
  Targets: `test_attach_and_render`, `test_cmdline`, `test_insert_and_quit`, `test_messages`, `test_multigrid`, `test_pixel_snapshot`, `test_popupmenu`, `test_resize`, `test_tabline`, `test_user_smoke`.
- **Tier 3 — `tests/qml/`** — `qmltest` against QML in isolation. Single target `test_qml`.

## Build/test cheatsheet

```pwsh
ctest --preset release --output-on-failure                              # all
ctest --preset release --output-on-failure -R '^test_grid_model$'       # single
ctest --preset release -R 'attach_and_render|resize|insert_and_quit'    # paint
ctest --preset release -R 'test_input'                                  # InputHandler
ctest --preset release -R '^test_qml$'                                  # QML
```

Register a new test by alphabetical insert in `tests/CMakeLists.txt` via `qvim_add_test(name path)`. Tier 2 tests must also be added to the `foreach(t ...)` list that sets the nvim-required `ENVIRONMENT`.

## Conventions

- **Start nvim via `startTestNvim(conn, extraArgs={})`**, not bare `conn.start(locateNvim())`. The helper (in `IntegrationHelpers.h`) prepends `--clean` so the developer's `init.vim` / `init.lua` doesn't leak into tests. Without it, assertions like "row 0 starts with 'abc'" silently fail on machines whose config adds `set number`, `set cmdheight=0`, etc.
- **For features intentionally gated off in `NvimConnector::attachUi`** (the `ext_*` diagnostic-mode flags), flip the test's asserts to expect the disabled behaviour and leave a clear comment naming the gate. The test then fails-loud the day someone re-enables the extension — that failure is the cue to restore the original asserts.

## Bug patterns to avoid

- Calling `conn.input("...")` directly from a smoke test — bypasses the focus chain. Use `QTest::keyClick(window, key)` so a focus regression actually fails the test.
- `QQuickWindow::grabWindow()` under `minimal` QPA — returns null. Use `QQuickItem::grabToImage()` on the subtree.
- Asserting exact `rows()` against the attach size with both `ext_messages: true` AND `ext_multigrid: true` — nvim relocates the message line off-grid only in that combination, shrinking the active grid by 1 row. Either extension alone keeps the grid the full attach size.
- Forgetting `QQuickWindow::setGraphicsApi(QSGRendererInterface::Software)` in `initTestCase()` for QML smoke tests — must be set before any window is created.
- Linking only `qvim_lib` for QML smoke tests — also need `qvim_libplugin` + `Qt6::Quick` to register the `Qvim` QML module.

## Templates

- Smoke / user-POV: `integration/test_user_smoke.cpp`.
- Plain integration over `NvimConnector` API: `integration/test_attach_and_render.cpp`.
- Headless unit: `unit/test_grid_model.cpp` (model snapshot) or `unit/test_input_handler.cpp` (encoding table).
- QML `TestCase`: `qml/tst_cmdline.qml`.
