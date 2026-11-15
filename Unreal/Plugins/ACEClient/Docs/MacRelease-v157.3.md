# AC:Unreal for Mac v157.3

This release combines the Mac client features on Thwargle's v157 source base
(`cc393efa50bcce5b90bdc08d5f68dd2d2b6acfa2`). The upstream version remains
`2026.11.13.157`; the Mac revision is 3.

- Resizable plugin bar with wrapping rows and columns, painted icons, small/large
  modes, optional names, independent opacity, drag reordering and pin controls.
- Item quality profiles, native ratings, useful spell combinations, Covenant armor,
  underclothes, summoning essences and Health/Healing Boost evaluation.
- Compact item hovers with readable quality badges, inline colored spells, value,
  workmanship and clear requirements. Tinkered and identified items receive ratings.
- Quality-based UCM loot goals, item categories, wield filters and multiple template
  profiles for collecting useful items for other characters.
- Clicked-item chat summaries retain armor level without listing every protection.
  The item name carries its quality color; the remaining text stays white.
- Native ACPedia search and Examine links, account/launcher improvements, expired
  inventory cleanup, closed-chest escape and complete item-casting motion.
- Upstream v156/v157 UCM automation, equipment and vendor improvements are retained.

The release branch merges the plugin-bar and Mac release histories and records
the older ACPedia proof of concept already restored and expanded in the hover
work. The loot-hover-v156/v158, loot-stats-v159 and loot-active-spells-v160 branches
are ancestors of this bundle. Historical pre-Mac-split desktop/archive checkpoints
remain preserved; their older code and generated materials do not replace the
newer implementations.

The macOS download contains `ACUnreal.app` and requires separate retail DAT files.
It targets Apple silicon and is ad hoc signed, without Apple notarization.
The matching Git tag and archive checksum identify the exact distributed source
and package. Thwargle, upstream ACE contributors, ACPedia and the AC community
retain their original notices and credits.
