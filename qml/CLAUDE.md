# qml/ — agent guide (delta)

Root `D:\qvim\CLAUDE.md` defines the reactive-proxy and focus rules. This file is the QML-side checklist.

## Conventions

- Connect QML to `NvimConnector` signals directly (`Connections { target: $connector }`). No imperative pull.
- Top-level scene is `Main.qml`; the grid host is `Shell.qml`. The long-lived item that owns focus is the `baseGrid` inside `Shell.qml`.
- Repeater delegates that need to react to per-id field changes consume a `Q_INVOKABLE QObject*` proxy (e.g. `$connector.gridFor(id)`), never a re-emitted whole-list.

## Why the reactive QObject-proxy rule exists

A `QHash<id, X>` re-emitted as a property nukes Repeater delegates on every nvim window event. That destroys `activeFocusItem` (dead cursor), per-`GridItem` glyph caches (flicker, FPS drop), and cursor blink phase. A proxy `QObject` whose fields are `Q_PROPERTY` with NOTIFY lets bindings re-evaluate in-place; the delegate instance is preserved.

## Focus lifetime

`Component.onCompleted: forceActiveFocus()` fires exactly once. If the owning item is later destroyed (e.g. Repeater rebuild) focus is gone. Focus must live on an item that is never destroyed across nvim window churn — `baseGrid` in `Shell.qml`, not a Repeater delegate.

## Bug patterns to avoid

- `model = null; model = list` to "refresh" a Repeater. Destroys focus, glyph cache, blink phase. Use a proxy + NOTIFY.
- `setContextProperty("foo", obj)` for a type that is already `QML_ELEMENT` — double-registers and shadows the QML import.
- Owning focus on a Repeater delegate. It will be destroyed.
- Layout reflows during `cmdline_show` (ext_multigrid fires `grid_resize` + `win_pos`) that destroy delegates — every grid container must survive resize.

## Build/test cheatsheet

```pwsh
cmake --build --preset dev
ctest --preset dev --output-on-failure -R '^test_qml$'
```

## Templates

- Scene root + `$connector` wiring: `Main.qml`.
- Long-lived grid host that owns focus and hosts Repeaters: `Shell.qml`.
- Overlay backed by a model with NOTIFY proxy: `Cmdline.qml`, `PopupMenu.qml`, `Tabline.qml`, `Messages.qml`.
