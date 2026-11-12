# Plugin icon artwork

`PluginIcons.png` is a transparent, painted 5-column × 4-row atlas. It is loaded
once and shared by the launcher, plugin-manager badges and window title icons.
The colors are painted into the artwork; do not tint the image with the toolbar
accent color. Custom `presentation.icon` PNGs continue to override built-in art.

The client stages this directory as UFS data. Loading uses Unreal's platform file
layer, so the same path works in editor and packaged clients without importing
twenty separate `.uasset` textures. Each square icon uses a normalized atlas cell;
transparent margins keep neighboring symbols out of the sampled area.

| Row | Column 1 | Column 2 | Column 3 | Column 4 | Column 5 |
| --- | --- | --- | --- | --- | --- |
| 1 | Plugins: gear | Action Deck: rune tiles | Auto Craft: mortar | Dereth Lens: magnifier | Hunt Ledger: trophy |
| 2 | Item Tools: parcel and wrench | Loot Editor: chest | Supplies: Prismatic Taper | Third Eye: eye | UCM: crossed swords |
| 3 | UCM Micro: levers | Waypoint: compass | ACPedia: book | UCM Log: scroll | Map: folded map |
| 4 | TreeStats: tree | Foundry: anvil | Cosmetics: mask | Other extensions: puzzle | Hyssop alternative |

`SACEPluginIcon::ArtworkSlot` is the runtime mapping. The Hyssop cell is available
as `supplyledger.hyssop`; the actual Supplies button uses the Prismatic Taper.
The small drag grip stays a vector control. If the atlas is unavailable, plugin
icons fall back to their vector symbols, including a rainbow taper for Supplies.

The artwork was created for AC: Unreal using the local game's Prismatic Taper,
Hyssop and Mortar and Pestle icons as visual references.

To see the updated bar in the Mac client, run from a normal Terminal session:

```bash
bash Unreal/Build/Mac/Preview-PluginBar.sh
```

This builds the development client, requires the icon renderer checks to pass,
then opens the game for a normal login. Test data uses an isolated profile;
the live client keeps the normal development-client settings. Logs are saved in
`Unreal/Saved/Automation/PluginBar`. Use `--test-only` to stop after validation.
The default checks are `ACE.Plugins.IconArtwork` and `ACE.Plugins.BarPresentation`:
they cover the staged texture, actual Slate button pixels, modifier-based pin
recovery, and the map's explicit Unpin action. `--full-checks` additionally runs
the broader UCM/profile/panel and keyboard/IME regression suites; their results
are reported separately rather than being treated as artwork failures.
