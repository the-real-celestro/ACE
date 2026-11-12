# Plugin bar build handoff

- Repository: `the-real-celestro/ACE`
- Branch: `codex/plugin-bar-redesign`
- Base: `61f826e04a9e1f277e42ce75669e31a37564de0e` (existing v158 client).

## Changes to build

- Shared transparent 20-icon painted atlas, staged as a runtime dependency.
- Small by default: separated 36-pixel buttons with 26-pixel icons; optional
  large mode retains 52-pixel buttons with 36-pixel icons. Horizontal/vertical layout,
  optional saved names, and open-window markers.
- Explicit Slate background tint fixes white launcher tiles.
- Control/Command reveal uses Slate modifier state while UI fields have focus.
- Right-click pin/unpin controls and an explicit Unpin button on the expanded map.
- Focused rendering, artwork, persistence and input regression checks.

Right-click any bar icon to toggle **Horizontal plugin bar** (off = vertical)
or **Large plugin bar** (off = small). Both switches also appear in Plugin
Manager's appearance settings, apply immediately, and persist across logins.

The feature diff excludes the new equipment-hover/spell-list work, TreeStats
uploader implementation and generated runtime-material changes from the shared
working directory. The base already includes earlier Mac/client work; this is
a feature branch on that existing base, not an upstream-only rebase.

## Build and check on macOS

Use Unreal Engine 5.8 and the normal project toolchain. From the repository root:

```bash
bash Unreal/Build/Mac/Preview-PluginBar.sh --test-only
```

Set `ACUE_ENGINE_ROOT` if the engine is not at `/Users/Shared/Epic Games/UE_5.8`.
The script builds `ACUnrealEditor Mac Development` and runs
`ACE.Plugins.IconArtwork` and `ACE.Plugins.BarPresentation` with a real renderer
and an isolated test profile. Do not use NullRHI for these rendering checks.
Logs and reports go into `Unreal/Saved/Automation/PluginBar/`.

Omit `--test-only` to launch the development client after checks pass.
The optional `--full-checks` adds broader HostAndPanel and KeyboardBindings
suites; those previously reported unrelated UCM-schema and offscreen IME errors.

## Validation status

The isolated diff passes `git diff --check` and the preview script passes
`bash -n`. Changed native sources were compile-checked during development.
The final isolated snapshot still needs the native build and renderer tests:
the local attempt stopped before compilation because the restricted execution
session could not write UnrealBuildTool's trace backup under the user's Library.

For an in-game smoke test, inspect both sizes and orientations and icon-only mode,
toggle optional names, open/close plugins, and pin/unpin a window and map while
the UI is locked and while a text field has focus. Verify saved settings after
relaunch. Do not merge or package a release solely on the compile checks.
