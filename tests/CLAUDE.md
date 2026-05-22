# tests/ — agent guide (delta)

Root `D:\qvim\CLAUDE.md` covers the three-tier philosophy. This file is operational.

## Tiers

- **Tier 1 — `tests/unit/`** — headless, `QTEST_GUILESS_MAIN`. No nvim.
  Targets: `test_cmdline_pos`, `test_grid_model`, `test_hidpi_rendering`, `test_highlight_table`, `test_input_fuzz`, `test_input_handler`, `test_msgpack_rpc`, `test_popupmenu_select`.
- **Tier 2 — `tests/integration/`** — real `nvim --embed`, `QT_QPA_PLATFORM=minimal`.
  Targets: `test_attach_and_render`, `test_cmdline`, `test_insert_and_quit`, `test_messages`, `test_multigrid`, `test_pixel_snapshot`, `test_popupmenu`, `test_resize`, `test_tabline`, `test_user_smoke`.
- **Tier 3 — `tests/qml/`** — `qmltest` against QML in isolation. Single target `test_qml`.

## Build/test cheatsheet

```pwsh
ctest --preset dev --output-on-failure                              # all
ctest --preset dev --output-on-failure -R '^test_grid_model$'       # single
ctest --preset dev -R 'attach_and_render|resize|insert_and_quit'    # paint
ctest --preset dev -R 'test_input'                                  # InputHandler
ctest --preset dev -R '^test_qml$'                                  # QML
```

Register a new test by alphabetical insert in `tests/CMakeLists.txt` via `qvim_add_test(name path)`. Tier 2 tests must also be added to the `foreach(t ...)` list that sets the nvim-required `ENVIRONMENT`.

## Bug patterns to avoid

- Calling `conn.input("...")` directly from a smoke test — bypasses the focus chain. Use `QTest::keyClick(window, key)` so a focus regression actually fails the test.
- `QQuickWindow::grabWindow()` under `minimal` QPA — returns null. Use `QQuickItem::grabToImage()` on the subtree.
- Asserting exact `rows()` against the attach size with `ext_messages: true` enabled — nvim relocates the message line off-grid; subtract 1.
- Forgetting `QQuickWindow::setGraphicsApi(QSGRendererInterface::Software)` in `initTestCase()` for QML smoke tests — must be set before any window is created.
- Linking only `qvim_lib` for QML smoke tests — also need `qvim_libplugin` + `Qt6::Quick` to register the `Qvim` QML module.

## Templates

- Smoke / user-POV: `integration/test_user_smoke.cpp`.
- Plain integration over `NvimConnector` API: `integration/test_attach_and_render.cpp`.
- Headless unit: `unit/test_grid_model.cpp` (model snapshot) or `unit/test_input_handler.cpp` (encoding table).
- QML `TestCase`: `qml/tst_cmdline.qml`.
