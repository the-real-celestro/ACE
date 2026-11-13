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
the affected stat in amber; inactive cantrip previews appear in blue. Armor
previews give an effective armor total and its added points, never a bare
resistance multiplier beside an armor-point total. Active spell totals and
their added amounts use green. Zero-percent bonuses are omitted, while useful
zeros such as weapon speed and damage variance remain visible. The neutral item
name is separate from the top-right quality badge; the numeric quality score is
in the footer. Stat labels align left and values align right, including Value,
Workmanship, and item type. Workmanship is displayed without an arbitrary combat
bonus: its usefulness depends on the operation being considered.

Spells share one indented rich-text flow in the body font, separated by middots.
Wrapping occurs at whitespace, including spaces inside a spell name; words are
never split to fill a line. Each known spell uses its contextual usefulness
color, rather than inheriting the item's overall grade or its name's rank alone.
Matching Legendary War Magic on a War caster is orange; irrelevant Cooking and
ordinary armor's negligible item-local banes are gray. Underclothes Impenetrability
and Covenant's carried protection retain their different relevance. A trained
crafting skill paired with supporting attribute cantrips from the actual DAT
formula can receive useful crafting colors on wearables, without inventing combat
score. Duplicate weaker cantrip ranks do not earn a second useful color.

Requirements have their own section: the required stat and amount appear on the
left, with the character's value and an independently green/red check on the
right. Lore activation and base-skill wield requirements remain distinct.
The panel fits its contents with shared measured label widths and consistent
section spacing.
There is no misleading percentage-of-maximum damage label. A hit interval such
as 34.5–65 describes that weapon's damage distribution, not the distribution of
possible weapon rolls. Damage-type text samples the same DAT effect palette as
the existing item icons; it does not falsely mark an untinkered item as rended.

## Rating policy

Standard summoning loot essences use a separate six-roll combination model.
Each of damage, damage resistance, critical chance, critical resistance,
critical damage and critical damage resistance independently ranges from 0 to
20; all six may reach 20 simultaneously. Quality is `100 × total / 120`, using
the same Poor-to-Legendary thresholds. The hover shows every roll, its maximum,
and the combined total; a missing bonus in a successful appraisal is a visible
zero. A failed appraisal never becomes a fabricated zero-quality item.

The ACE generator has a 50% chance to roll each rating. A rolled rating is an
inclusive d10 plus a second d10 with probability `0.4 + 0.02 × loot tier`.
Tier changes probability, not the 0–20 limits; quality is not a drop percentile.
The checked-in essence reference covers all 252 generated templates across pet
levels 50/80/100/125/150/180/200, preserving their actual use brackets. Current
Summoning, specialization, user level, mastery, Lore and charges determine
availability without reducing intrinsic roll quality. Workmanship, external
player bonuses and remaining charges earn no quality credit. Unsupported custom
ratings outside 0–20 remain visible but unrated. Noncombat pet devices are
outside this random-loot comparison.

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
If protection is unverified and the item has no useful combat spell, set, or
native-rating package, its overall grade is **Unrated** rather than a fabricated
Poor grade. The observed stats and contextual spell colors remain visible.

Wearable spell-package rank factors are Minor .25, Major .55, Epic .94 and
Legendary 1.0, adjusted for the character's build. Contributions are sorted by
usefulness, then discounted by 1/.20/.10/.05 and progressively halved thereafter.
Useful high-rank attributes and defense skills therefore remain major benefits
even on ordinary clothing. The displayed spell contributions add to the stated
package total. Impenetrability is a primary benefit on underclothes: Epic earns
94 and Legendary 100 package points before other useful spells. A small native
armor level on clothing must not turn that into the lower armor-package weighting.
Ordinary item banes are deliberately negligible: they bypass the wearable package
formula and add at most 0.20 for Epic or 0.25 for Legendary, reduced by remaining
resistance headroom at the 2.0 cap. They are distinct from character-wide wards
and the Armor cantrip. Wield and activation failures affect usability, not quality.

Covenant armor and Covenant shields have a separate protection-first model:
20 points for native AL in the exact wield/level cohort, 60 for effective
seven-type protection using earned permanent values and only the item's own
ordinary spells and cantrips, and a 20-point useful combat-cantrip package.
The protection component is 75% mean and 25% weakest normalized protection.
Its ordinary-spell benchmark is the bracket's maximum AL plus 240 Impenetrability,
with each protection capped at 2.0. Stacking Impenetrability cantrips may exceed
that benchmark; excess bane strength at the cap earns nothing. Relevant sets and
native gear ratings retain their separate credits. Covenant shield AL ranges
remain separate from Covenant armor ranges. Ordinary armor's negligible bane
credit and 80-point spell-package weighting do not apply to Covenant.

The hover labels Covenant ratings **item spells**: carried spells are potential
when inactive, and active totals remain server-derived. Outside buffs and future
tinkers are never assumed. A clamped active bane does not reveal the original
resistance; interval reasoning can establish capped self protection without
inventing a base roll. Only known pending cantrip gains get a blue preview.
Unverified spells, protection or generation brackets cannot receive a fabricated
full Covenant grade. Loot Olthoi armor remains a distinct reference family.

The character-wide Armor cantrip has a .80 relevance factor; it is not an
item-local bane. Maximum Health/Vitality and Healing Boost native ratings each
add 8/18/30 quality points at rating 1/2/3. These are separate, broadly useful
survival benefits: they add to spell and set credit without spell-package
discounts. Higher custom ratings retain order at +10 quality points per extra
rating above 3; no false natural maximum is displayed. Other native gear ratings
retain 2 quality points per rating point. The user's requested health weighting
does not apply to the separate six-roll essence model.
Equipment-set potential is read from the highest actual SpellTable set tier,
using its strongest build-relevant effect: a +20 primary combat skill or primary
attribute earns 20 quality points; secondary attributes and supporting effects
earn less. Duplicate magic-school effects do not multiply that credit. Unknown
sets and unrelated crafting effects do not receive invented combat bonuses.
For a mage, an Adept's piece with Epic Armor scores 60.16 + 20 = 80.16 (Rare).
An armor item outside verified roll cohorts labels this **set + spells** so the
score does not imply a measured original-protection percentile. The set ID from
active enchantment messages is retained: the hover marks an active set separately
and shows relevant, server-reported set additions in green. An incomplete set
retains its piece potential without claiming the full set's stats are active.

A reliable current zero-tinker appraisal supersedes older original-roll cache
values. Missing original dimensions are unknown, never zero-valued tinkers.

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

`ACE.LootQuality.SpellContext` checks the Hybrid header/footer layout, actual
DAT spell identities and crafting formulas, per-spell usefulness colors,
independent Lore/wield results, right-aligned value columns, and one inline
spell flow without forced per-spell breaks. It renders normal, blocked-Lore,
unrated-armor, and supported-crafting examples to `Saved/LootQuality-hybrid-*.png`.
The retained-popup test also changes spell color without changing combat score
to verify that the existing hover updates without replacing the popup.

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

### Active spell totals (v160)

Hover and Examine use the server's spell-inclusive appraisal totals. Beneficial
changes are green; harmful changes retain red. Verified active contributions
appear inline, for example `Damage Bonus: 18 (+16 spells)` and
`Melee defense: +41% (+29% spells)`. Melee damage retains its per-hit range and
shows the change at both endpoints. Launcher damage modifiers use the same
percentage notation as Examine. Tinkering remains a separate permanent change.

Unbuffed values come from this instance's raw weapon properties or from the
known, category-stacked active spell effects in its appraisal. Innate inactive
spells do not change current totals. Already active cantrips are not repeated as
an additional blue preview. Unknown effects and irreversible capped values keep
the supplied total/highlight without inventing a spell amount. Speed at zero
uses its actual reduction when a raw instance speed is available. The former
verbose Buffed/Unbuffed footer is replaced by inline contributions.
Enchantment updates queue fresh appraisals for tracked equipped items, using the
existing throttled background path. The previous panel stays visible while the
server supplies new totals.

`ACE.RetailParity.ItemSpellTotals` checks final bow damage, clamped speed,
ordinary/cantrip stacking, inactive and unknown effects, debuffs, caster PvE/PvP
bonuses, multiplicative mana conversion, shared Examine colors and the rendered
hover. Its native capture is `Saved/LootQuality-active-spells.png`.

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


## Local v169: estimated histories and shared chat summaries

Observed zero-tinker originals remain exact and are never replaced by guesses.
When an identified tinkered item has no saved original, the evaluator enumerates
legal inverse ACE operations within its tinker budget and verified class/wield
ranges: iron +1 damage, granite ×0.8 variance, mahogany +0.04 launcher multiplier,
velvet/brass +0.01 offense/defense, green garnet/opal +0.01 caster bonuses, and
steel +20 AL or matching +0.4 elemental-resistance tinkers. An imbue reserves
one attempt. Unassigned attempts can represent speed, value, burden or other
changes. Heuristic operation preferences choose a coherent feasible history;
they are neither measured drop odds nor proof of the actual salvage used.
The review retains the feasible score interval and labels inferred bases and
increments with ≈ and the overall score Estimated. Guessed originals are not
written to the observed-original cache. If inversion fails for a known tinkered
weapon, a clearly estimated comparison of its current permanent stats is used
without fabricating original values or tinker additions.

Modern skill wield gates take precedence over legacy appraisal weapon-skill
labels. Workmanship-bearing templates missing an exact WCID can use an estimated
comparison to supported weapon class, shape and wield ranges; unsupported
brackets do not acquire invented maxima. No-wield utility casters have a defense
and mana-efficiency model. Plain identified jewelry/underclothes with an empty
spell package receive Poor rather than remaining Unrated.

Click inspection uses the same current-stat formatter and rating as hover:
`[LEGENDARY] Name - per-hit range Fire - Melee D ... - Work ... - Value ...`.
The local chat line uses the quality color, skips zero-percent bonuses and marks
estimated scores. Remaining Unrated results disclose a short reason; fixed
quest items may show full stats without a random-generation grade.

## Local v170: support effects and complete scoring audit

Arcane Lore now earns useful support-skill credit. A single Legendary Arcane
Prowess on jewelry scores 80 (Rare); an ordinary carried protection spell adds
no independent quality points. Trained buff schools, Healing, Shield and other
supported secondary skills retain contextual support credit. Wearables consider
every trained offensive skill, rather than choosing only the highest skill, and
attribute relevance follows those trained builds. Summoning's governing
attributes come from the DAT skill formula.

The reference exporter groups cantrips by canonical effect, including renamed
legacy weapon skills and the older Blood Thirst stat keys. Lower ranks use the
matching Legendary denominator, and aliases of one effect cannot stack quality.
The header quality badge uses larger bold text and contrasting ink on a solid
quality-color fill; the item name remains neutral.

`ACE.LootQuality.RatingAudit` calls the production evaluator for all 297 decoded
cantrip IDs across 73 item/build fixtures and all 139 DAT sets, exporting
`Saved/Loot-rating-audit.json`. It also records all 333 weapon brackets, 195 armor
brackets, native rating weights and 4,098 ordinary decoded stat effects. Fixtures
exercise mechanical relevance, including spell/slot combinations that do not
naturally generate. These utility scores are not loot probabilities or official
server rarity. Single-effect contributions are measured against each fixture's
no-cantrip baseline; multiple wearable spells use diminishing returns.

The audit deliberately retains zero-credit effects. Crafting synergy currently
changes usefulness colors, not overall quality; regeneration, several utility
skills, and raw crit/slayer/imbue/proc properties lack independent quality models.
Zero modeled credit does not prove an effect worthless. Workmanship, value,
burden, item mana and spellcraft remain displayed without a flat quality bonus.
Health Gain regeneration is separate from the strongly weighted Maximum Health
and Healing Boost native gear ratings. Unsupported fixed/custom generation
brackets can still show actual stats without an invented random-loot grade.

## Local v171: whole-model calibration

This supersedes the v170 point weights above. All six attributes always earn
positive credit, on weapons and wearables. Primary use, trained supporting skill
formulas and universal survival value adjust priority rather than discarding an
attribute. Endurance's survival factor is .98; trained Shield is .82. Focus and
Willpower receive .94 when supporting a trained skill, or a .90 baseline when
there is no matching trained skill. Wise's attribute potential is therefore
positive for melee builds too.

A jewelry/clothing spell package now uses 85 points times relevance and rank,
with diminishing extra spells as before. Ordinary armor uses a 68-point package
plus its original protection; Covenant keeps its separate 20/60/20 model.
The highest single cantrip contribution is underclothes Impenetrability at 89.25,
so a single useful Legendary cantrip cannot itself create a 100-point item.
Spell usefulness colors still describe the spell's contextual usefulness, which
is separate from the number of points it contributes to the full item.

Crafting now contributes to quality using the same training and DAT attribute
synergy as its color. Regeneration, movement, lockpick, social and appraisal
effects also receive explicit credit. Untrained offensive skills can still be
irrelevant to the current template; Swift Hunter remains zero by the chosen
castable-speed policy. Ordinary item banes retain their low capped credit, and
Covenant's own banes remain part of its actual self-protection calculation.

Native ratings are now normalized to their actual ordinary item bracket: armor
can reach +2, clothing/jewelry +3. Best-roll credits are Maximum Health 50;
Healing Boost, Damage and Damage Resistance 45; Critical Damage and Critical
Damage Resistance 35; Critical Chance and Critical Resistance 30; Nether and
Life Resistance 25. Clothing/jewelry use 25%/60%/100% of that credit for +1/+2/+3;
armor uses 40%/100% for +1/+2. Higher custom values preserve ordering without
claiming an ordinary generation range. These are chosen quality weights, not
percent increases to actual combat benefit. Sets also recognize vital and
native-rating effects and give positive universal attribute potential. Distinct
set effects combine at 100%/30%/15%/7.5%/… credit, capped at 36 points; duplicate
targets use their strongest effect. Wise therefore receives credit for its
Focus, Willpower and mana package rather than only its strongest attribute.

Simple UCM goals have an independent usability choice: Any, Usable now, or Usable
now or close. Close permits at most 25 skill/Lore points, 10 attribute points, or
5 levels of numeric shortfall. Training, specialization, mastery, restricted
activation and server-only gates are never assumed; empty charges/mana fail the
filter. The shared wield validator still handles all four wield gates. Essence
use gates are checked separately from their roll quality. Neither usability nor
a character's current skill cap subtracts from the item's quality score, and
the existing exact/range/no-skill wield filters can be combined with usability.

The complete native audit asserts positive attribute credit across every profile,
positive Wise potential for every wearable build and less than 100 points from
each individual cantrip. These complement the actual pickup-policy usability
tests, rather than requiring the player to discover each missing category.

## Local v172: recipients, trade loot and named templates

Simple goals now choose a rating recipient independently of the looting
character: Current character, Universal / trade loot, or one or more templates.
Universal means useful to at least one supported coherent combat/crafting build,
not useful to every possible character simultaneously. It compares complete item
scores, so a Heavy-only bonus and a War-only bonus cannot be combined from two
different recipients. An explicitly saved hybrid can value both trained skills.
Weapons and essences also require the corresponding trained combat/Summoning
identity in the recipient template; shields require Shield training.

Built-in templates provide combat, Summoning, crafting and Sneak/Deception
starting points. Manage named templates saves a friend/alt name, main skill and
trained skills with the shared Loot profile (`loot_rating_templates`). A preset
can be used immediately or customized, and Copy my trained skills captures the
current character's training. These are quality preferences, not invented copies
of another player's numeric stats. Wield brackets remain independent goal
filters. Current-character usability is hidden and ignored for recipient/trade
goals, including profiles written outside the UI.

Multiple recipients use OR matching at the requested minimum grade, retaining
the best eligible complete score. Unrecognized/deleted IDs fail validation and
never fall back to the looter. The UI prevents deleting an in-use template;
renaming retains its stable ID. Current-character behavior is the default for
existing goals. Estimated-score filtering applies to each recipient, and
Universal separately retains the best exact score when estimates are disabled.
Pickup status and the resulting local rule explanation name the matching
recipient. Personal hover/icon ratings remain personal.

Recipient scores reuse observed appraisals/originals. Cached summaries are
invalidated per appraised item, on identity changes, enchantment refreshes and
character/server changes. Saved template signatures change with trained skills.
At most eight new recipient evaluations and roughly 2 ms of evaluation work are
started in a frame (an individual call may exceed the time allowance). Pending
scores yield the loot scan instead of requesting repeated appraisals or treating
an incomplete Universal comparison as final. Category filters limit evaluation
to relevant goals, and template scores contain no full tooltip trees.

`ACE.LootQuality.TargetTemplates` exercises native friend/universal scoring,
coherent versus hybrid packages, schema references, cache reuse/invalidation,
target weapon fit, OR matching, pending work, estimated filtering and independence
from looter usability. `ACE.Plugins.SimpleLootGoalsUI` checks saved recipient
selection and shared Loot-section export, with native universal, multi-template
and named-template-editor captures.
