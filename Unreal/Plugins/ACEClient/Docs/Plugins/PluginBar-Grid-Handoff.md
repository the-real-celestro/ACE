# Resizable plugin bar — build notes

Branch: `codex/plugin-bar-redesign`. This update builds on `2fdf72d81666375716593471183afa7843bcf362`.

The launcher now uses a fitted, row-major grid with no scroll container. Hold the
existing Quick-show binding (Control / Command by default on Mac) to reveal the
move/minimize header and resize corner. Resizing width selects columns; resizing
height selects rows. Row, column and four-column presets are in the icon/handle
context menu. A minimized handle remains available for recovery and offers the
same context menu. Hiding the header preserves icon positions.

Background opacity is edited in that menu or either theme's Plugin Manager
appearance settings. It fades panel/tile backgrounds, retains painted art and
labels, and is independent of the all-plugin opacity option. The bar stores its
opacity under `_window_opacity.__bar`, and custom columns under
`_plugin_bar_columns`. Changing the row/column preset clears the custom columns;
changing icon size or labels preserves them. Temporary viewport fitting does not
replace the preferred column count.

Drag reordering remains shared with Plugin Manager and works across grid rows.
Icon menus additionally offer Move earlier / Move later using visible grid order.
Execution order is unchanged. Other plugin windows are retained while reflowing.

## Validation

The four changed C++ translation units (desktop, manager, window settings and
tests) pass direct compiler syntax/type checks with installed UE 5.8 headers.
A standalone executable linked to UE Core also passed 4,166 checks of the actual
layout header: both icon sizes, all requested column counts for 1–64 icons,
viewport fitting, row/column/square presets and resize quantization. These same
exhaustive viewport cases are included in BarLayout.
Script syntax and whitespace checks pass. These are not a linked editor build or
renderer acceptance. The native build and macOS renderer startup were blocked
in the earlier verification session; this update still needs those checks from
a normal macOS application session.

```bash
bash Unreal/Build/Mac/Preview-PluginBar.sh --test-only
```

The focused filter now includes `ACE.Plugins.BarLayout`, which checks grid
geometry, resize quantization and viewport fitting. `BarPresentation` checks the
actual sixteen-target Slate grid, cross-row drops, header/resize visibility,
stationary icons during reveal, transparent backgrounds with retained opaque
artwork, and grid/opacity persistence after recreating the dock. It emits the
usual size/orientation captures plus `PluginBar-Checked-Grid-*.png` for small/large,
labels/icons, and full transparency.

Live checks for the builder:

1. With the existing screenshot's plugin set, confirm no scroll bar or wheel
   movement. Check both themes and small/large icon modes.
2. Hold/release Quick-show with a chat field focused. Confirm the header and
   resize grip reveal/hide, with stationary icons and no empty title gap.
3. Resize to a row, a column and a four-column grid. Verify every icon is
   clickable, including the last partial row. Repeat while pinned/locked using
   Quick-show. Check resizing across a UI scale or viewport-size change.
4. Adjust opacity from fully opaque to transparent and back through the icon
   menu and Plugin Manager. Artwork, names and open markers should remain visible.
5. Drag icons across rows and check Move earlier / Move later; relaunch and check
   the order, grid shape, position, transparency and minimized state.
6. Minimize, release Quick-show, and recover through the handle, its right-click
   menu and Plugin Manager. Reflow must not close open plugin windows.

Only plugin-bar sources, tests, documentation and the preview filter belong to
this update. The pre-existing local `PluginBar-Build-Handoff.md` edit and
`PluginBar-Validation-2026-11-12.md` are not part of its published file set.
