# Loot quality and hover breakdown

The inventory, equipment, container, trade and vendor views use the original DAT
artwork. A rated item gets a WoW-style beveled quality frame around its square slot: gray, white, green, blue,
purple or orange. Failed wield requirements change the background to plum without
changing the quality edge. Small H/L/F/2H marks are drawn only where the actual
foreground artwork leaves room. The complete class name is always in the hover.

The hover uses concise Examine-style stat lines, without sliders. The main
values come directly from the appraisal, including active spell effects.
Verified natural limits appear inline, for example `+18% (base max +20%)`.
Variance uses `base best` because lower is better. These limits do not claim
to cap tinkering or spells. Observed original/tinkering changes appear below
the affected stat in amber; intrinsic cantrip contributions appear in blue.
There is no misleading percentage-of-maximum damage label. A hit interval such
as 34.5–65 describes that weapon's damage distribution, not the distribution of
possible weapon rolls. Damage-type text samples the same DAT effect palette as
the existing item icons; it does not falsely mark an untinkered item as rended.

## Rating policy

These are proposed combat-utility weights, not the server's rarity or drop odds.
The comparison group is weapon class/subtype/strike pattern and wield requirement,
across eligible ordinary loot tiers. The public ACE baseline is explicitly named.

| Component | Original melee quality |
| --- | ---: |
| Average base hit, incorporating maximum and variance | 85% |
| Melee attack modifier | 10% |
| Weapon melee-defense modifier | 5% |
| Speed in the standard buffed/capped profile | 0% |

The damage component compares the item's mean with both legal endpoints. A
perfect damage interval with the worst other bonuses earns 85, which is Rare.
The highest maximum damage with a wide variance does not automatically earn 85.
Fixed components redistribute their weight; missing/unknown components do not.

Legendary cantrip additions for melee are Blood Thirst +8, matching weapon skill
+5, Heart Thirst +5, Defender +6, Melee Defense +5 and Magic Defense +5. A primary
damage attribute adds up to 4.2 and a secondary attack attribute up to 2.8.
Actual native effect amounts scale lower ranks within each family. Multiplicative
effects are measured relative to 1.0, so a weak ward cannot outrank a strong ward.
Unrelated crafting spells earn zero combat points. Auxiliary skills need relevant
training. Quickness can retain skill/defense value even when speed is capped.

Casters use 85% matching spell damage and 15% weapon melee defense. Legendary
Defender adds 12; Melee/Magic Defense each add 7. Maximum spell damage, maximum
weapon defense and Legendary Defender scores 112. Spirit Thirst must have an
applicable elemental caster; it is not Blood Thirst. Launchers have their own
60/25/15 damage-multiplier/elemental-bonus/weapon-defense mix, redistributing fixed
dimensions. Heart Thirst does not supply missile accuracy.

Verified armor has a separate model: 20% original protection and 80% useful
spell package. The protection component includes armor level and the four
mutable elemental multipliers. Armor has separate normal, extremity, shield,
Covenant and Olthoi generation cohorts; tier-eight Legendary/gear-rating level
promotions preserve the earlier legal AL support. Society/custom exceptions
remain outside that baseline. Clothing, jewelry and armor without a verified
original protection baseline explicitly report **combat spell-package quality**.
They never pretend their armor/protection has been scored.
Unknown armor protection retains the 80% spell weighting and reports a lower
bound; losing original evidence cannot increase the item's rating. Missing or
out-of-range resistance inputs cannot be silently dropped from protection quality.

Wearable spell-package rank factors are Minor .25, Major .55, Epic .94 and
Legendary 1.0, adjusted for the character's build. Contributions are sorted by
usefulness, then discounted by 1/.20/.10/.05 and progressively halved thereafter.
Useful high-rank attributes and defense skills therefore remain major benefits
even on ordinary clothing. The displayed spell contributions add to the stated
package total. Native gear ratings add 2 quality points per rating point; useful
standard combat equipment sets add 4 potential points, separately from cantrips.
Set points do not claim that an incomplete set's effects are active.

| Grade | Quality points |
| --- | ---: |
| Poor | below 20 |
| Common | 20–below 50 |
| Uncommon | 50–below 75 |
| Rare | 75–below 90 |
| Epic | 90–below 97 |
| Legendary | 97 and above |

The named top tier is an entry threshold. Quality points have no global ceiling,
so exceptional combinations continue to rank above ordinary Legendary items.
The individual stat lines show actual values, independently of these heuristic
overall quality points.

## Evidence, requirements and stacking

Appraisal weapon profiles and some floating properties already contain spell
effects. Known active spell categories are resolved using the local DAT's category
and power metadata, then separated before evaluating permanent/original stats.
Intrinsic spellbook entries and high-bit active entries are distinct. Duplicate
cantrip ranks do not add. Original quality excludes spell effects; the cantrip
bonus is not counted a second time as part of the original damage roll.

An item retains its intrinsic potential grade when it cannot yet be wielded.
Raw-skill wield checks use Base, not buffed Current. Arcane Lore, activation skill,
mana and restricted activation are shown separately. Confirmed active effects,
potential effects and cantrips already covered by current gear have distinct
labels. The quality edge describes useful potential, not a promise of an immediate
upgrade over equipped gear. The server remains authoritative for every action.

Successful untinkered appraisals preserve an original snapshot locally, isolated
by server/character and checked against item identity. Subsequent tinkering can
then show exact observed changes. Tinker count alone cannot recover a previous
maximum or variance. Pre-existing tinkered items explicitly show unknown original
stats; wearable spell packages can still be assessed independently. Failed
appraisals, unsupported items, unresolvable effects and values outside the public
baseline are not silently converted into perfect original rolls. Fixed-stat
utility casters and uncalibrated special systems remain explicitly unrated.

Appraisals use the existing background path, at most one maintenance request at
a time and no more than two per second. Nothing is equipped, consumed or looted
by the rating subsystem. Unchanged profiles retain cached results; health ticks
alone do not require reconstructing every tooltip. Private snapshots, DATs and
account data are never part of a release.

## Reference data and checks

`Private/Loot/ACELootReference.inl` contains compact numeric facts from public
ACE source/world data. Its adjacent manifest records source revision, input
world hash and output hash. It includes 41 weapon variants, 333 class/wield
cohorts, 216 ordinary armor templates, 195 armor cohorts and 297 cantrip effects.
It also retains ordinary native spell effects needed to separate active buffs.
No original game DAT file is embedded.

Regenerate with `Tools/LootQuality/export-reference.py --research <research-dir>`.
The inputs are `weapon-ranges-by-tier.json` and `loot-reference.sqlite` from the
research export. Generation follows `LootGenerationFactory_Clothing`,
`MutateFilters`, the mutation scripts and active cantrip pools. A custom server
needs its own verified generation reference.

`ACE.LootQuality.Scoring` exercises supported class/wield endpoints, reflected
spells, duplicate cantrips, defense packages, original/tinker snapshots, Lore/raw
skill checks, custom outliers, armor promotion and the exceptional caster case.
With rendering enabled it writes `Saved/LootQuality-hover.png`. Use
`-RetailDatDir=<local DAT directory>` and an isolated `-UserDir`.

The standalone `Tools/LootQuality/test-scoring.cpp` checks 7,605 combinations,
monotonicity, grade boundaries, relevance and continued ordering above 100.
`review-examples.cpp` emits the review examples using the same shipped arithmetic.
Also run the existing item presentation, icon rendering, tooltip and inventory
performance regression suites before distributing a build.

## Concise stats and special items (v159)

Stat display is independent of random-loot rating eligibility. Quest and custom
weapons show their reported damage, defense, mana conversion, critical chance,
slayer modifier, item mana, spellcraft and other supplied combat properties.
All intrinsic spell names are retained, including ordinary auras and activated
spells. Active high-bit spell entries do not duplicate those names. Missing or
non-finite properties are omitted. Fixed/special weapons without a verified
generation cohort do not receive invented comparison limits or roll grades.

Examine and the hover share the same special-property formatter for slayers,
imbues/rending, bonding, attunement and other flags. Armor shows all supplied
effective resistance levels rather than only the dimensions used in its score.
`ACE.LootQuality.SpecialItems` exercises the reported Rynthid Tentacle Wand case,
compares current totals with Examine, checks ordinary spells and properties,
and renders `Saved/LootQuality-quest-hover.png`. It also checks missing and
non-finite values. Existing scoring and hover-refresh fixtures remain in place.

## Icon and hover stability

Quality uses an original beveled square frame inspired by WoW: a dark outer
rim, rarity-colored faces, an upper-left highlight and a recessed inner seam.
It scales with larger icons and leaves interior artwork and imbue backgrounds
untinted. Class marks only use clear foreground pixels inside the frame.
Repeated instructional paragraphs, color
legends and scoring explanations stay out of the hover.

Authored underlays and imbue/effect backgrounds retain their original DAT colors,
including on items the character cannot wield. These items use a small pink
lower-right corner marker instead of the unusable background tint; their quality
edge remains independent. Ordinary unusable equipment retains its distinct tint.

Desktop item hovers use one controller-managed, noninteractive viewport panel.
The binder checks exposed item geometry directly, including HitTestInvisible
paperdoll icons. Backpack, equipment, shortcuts, containers, vendors and both
trade lists use the same route. Slate's automatic item popup is disabled on
desktop; content updates retain the attached panel and its root. Other windows
and menus covering the canvas suppress covered item hovers. VR/mobile retain
their existing tooltip route.

`ACE.LootQuality.HoverRouting` drives the native controller through all seven
item surfaces and 100 rendered appraisal/refresh cycles, asserting stable panel
attachment and no duplicate automatic popup. `ACE.LootQuality.IconBorders`
renders both 32- and 64-pixel icons and checks four-sided color, bevel shading and clear interior artwork.
The existing scoring, inventory, tooltip, icon and background fixtures also run.

On macOS the retail cursor artwork is installed as native OS cursor shapes,
including targeting, busy, move and resize states. Scaling updates artwork and
hotspots together. Native drawing keeps the cursor visible over macOS overlays
such as notifications; other platforms retain their existing cursor path. Missing
native art falls back to the software cursor, and controller teardown restores
the previous cursor mapping policy. `ACE.RetailParity.MacNativeCursor` checks
native color/alpha, hotspots, scaling, reuse, and cleanup. Notification interaction
itself requires a desktop check; offscreen tests do not simulate Notification Center.

The v157 shutdown fix drains cached loot-filter ICU patterns on the engine's
pre-exit event, before ICU resets its allocator. Compiled templates have shared,
synchronized ownership; shutdown waits for in-flight matching and blocks late
calls. Thread-local text-only validation/predicate caches hold no ICU objects.
`ACE.Plugins.RegexShutdown` warms templates from game and worker threads, checks
drain/restart, and leaves a warm cache for the native process-exit check.

For the shutdown regression use
`-ExecCmds="Automation RunTests ACE.Plugins.RegexShutdown;SoftQuit"`. Do not
combine this with `-TestExit`: on macOS that option calls `_Exit` and skips the
normal engine cleanup. Require a zero process exit and a pre-exit cache-drain
log entry after the test queue completes.
