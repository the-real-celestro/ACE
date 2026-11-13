# Loot matching for Lua plugins

`lootmatch` is the shared evaluator used by UCM and Item Tools. The host loads it
into every plugin VM; no `require`, reserved plugin ID or running UCM is needed.
The implementation is [ClientMods/loot_rules.lua](../../ClientMods/loot_rules.lua).
Use it to classify inventory/corpse items, preview loot rules, or choose an action
in your own automation policy. Matching alone never appraises, picks up, equips,
salvages, sells, reads an item, or writes a chat message.

## Signature and results

```lua
local result = lootmatch(rule, item, snapshot, scan)
```

- `rule`: one native loot-rule table, not the full profile or rule list.
- `item`: one host item snapshot, such as an entry in `snapshot.inventory` or
  `snapshot.contents`. Keep its property maps and `identified` flag intact.
- `snapshot`: the current callback snapshot. Some imported conditions read
  `level`, `skills` and `main_pack_slots` from it.
- `scan`: a mutable table, initially `{}`, owned by this particular rule/item
  evaluation. Always supply it, even when expecting the rule to finish immediately.

The four possible results have different meanings:

- `true`: this rule matches the available data.
- `false`: this rule does not match, is disabled, or required data remains absent
  after identification.
- `nil`: a potentially matching rule needs additional appraisal data. Request an
  assessment through the appropriate action permission, wait for new data, then
  start again with an empty `scan`. Failed assessment is not a zero-valued property.
- `"pending"`: the callback's cooperative work allowance is low. Keep `scan` and
  resume the same rule/item next callback. Do not advance to the next rule yet.

**Use `result == true`, not `if result then`.** Lua treats `"pending"` as true.
Discard the scan after any completed result. Also discard it if the rule changes,
the item changes/disappears, its appraisal/inventory revision changes, or a
character-state value used by the rule changes. Never share one cursor between
different items or different rules. Store cursors in local Lua state across
callbacks; do not persist these temporary implementation details in saved state.

## Basic rule fields

`enabled=false` always fails; omission enables the rule. `name` defaults to no
name filter. `name_mode` is `prefix` by default and also accepts `exact`,
`contains`, `any`, `all`, or `regex`. Name matching is case-insensitive. `any` and
`all` split `name` on literal `|`, trim surrounding whitespace, ignore empty terms,
and match literal substrings; use `regex` for expressions such as `sword|unarmed`.

`material` is a material ID, with 0/omitted meaning any. `type` is an item-type
bitmask; any overlapping bit matches, and 0/omitted means any. These are public
item filters, checked before appraisal-dependent conditions.

`min_value`/`max_value`, `min_workmanship`/`max_workmanship`,
`min_burden`/`max_burden`, and `min_rating`/`max_rating` constrain the corresponding
item fields. Rating uses `item.damage_rating`. Bounds are inclusive; a positive
bound enables that side of the filter and zero means no bound. Missing values
can require appraisal; a received zero is a known value.

`conditions` is an array of up to 64 conditions. `conditions_mode="all"` (default)
requires all conditions; `"any"` requires at least one. Basic name/type/material
and range filters still apply in both modes. An empty conditions array adds no
restriction. A known false condition can finish an AND rule despite unknown
alternatives; a known true condition can finish an OR group without appraising
the other alternatives. Cheap conditions run before string/spell searches.

Fields such as `label`, `action`, `keep_up_to`, and `count_by_name` are policy
metadata, not predicates evaluated by `lootmatch`. The caller chooses rule order,
quantity limits and actions. Native profile actions are `keep`, `skip`, `salvage`,
`sell`, and `read`; a matching skip rule is still a successful match. UCM and
Item Tools stop at the first applicable rule, waiting for assessment/pending work
before considering later rules. Reproduce that ordering if you want their behavior.

## Numeric property conditions

```lua
local rule = {
  label = "Light or Finesse weapons", action = "keep",
  conditions_mode = "all",
  conditions = {
    {field="int", key=218103840, op="in", values={45,46}},
    {field="int", key=19, op="ge", value=1000}
  }
}
```

`field="int"` reads `item.int_properties`; `field="float"` reads
`item.float_properties`. `key` is a numeric property ID; map keys in the snapshot
are decimal strings, for example `item.int_properties["218103840"]`.
Weapon Skill's compatibility property is **218103840**; native WeaponSkill is
**48**, and WieldSkillType is **159**. Skill IDs such as Light Weapons **45** and
Finesse Weapons **46** are comparison values, not property keys. Server-specific
numeric IDs are supported when supplied by the server.

Operators are `eq`, `ne`, `ge`, `le`, `bits` and `in`. The first five use `value`;
`bits` tests **any overlapping bit**, not whether all requested bits are present.
`in` uses a nonempty `values` array of at most 64 finite numbers. A missing
numeric property on an unidentified item returns unknown. After identification,
missing properties fail unless `missing_zero=true`, which compares them as zero.
This option does not turn an unassessed property into a known zero.

## Text and spell conditions

```lua
local description = {
  field="string", key=16, pattern="sword|unarmed",
  match_mode="any", ignore_case=true
}
local spell = {
  field="spells", pattern="Legendary .* Aptitude",
  exclude="Legendary Cooking Aptitude", value=1
}
```

`field="string"` reads `item.string_properties[tostring(key)]`. Name (key 1)
falls back to `item.name`. After appraisal, an absent string is matched as `""`;
before appraisal, an absent non-name string is unknown.

`match_mode` defaults to **regex** and also supports `contains`, `exact`, `prefix`,
`any`, and `all`. Literal modes ignore case unless `ignore_case=false`. Regex
mode is case-sensitive unless `ignore_case=true` or the expression supplies a
case-insensitive modifier. `any`/`all` use trimmed, pipe-separated literal terms;
they are not regex alternation. This default differs from the basic name filter.

`field="spells"` requires identification and searches `item.spell_names` using
regex `pattern` and optional regex `exclude`. `value` is the minimum number of
matching spell names (integer 0–1024, default 1 for a direct helper call). For
case-insensitive spell matching, put `(?i)` in the expression; `match_mode` and
`ignore_case` do not control spell conditions. Long spell scans can yield pending.

The native profile validator limits condition patterns/exclusions to 2,048
characters and validates the supported VT regex subset. Invalid or unsupported
patterns can stop a directly authored script; `pcall` is unavailable. Prefer
literal matching when a regex is unnecessary.

## Imported Classic conditions and validation

`field="legacy"` carries a supported Classic requirement ID in `key` and its
numeric `args`; some palette checks also use `slots`. Supported IDs are
6, 7, 10, 14–17, 1000–1004, 2000, 2001, 2003, and 2005–2008. These cover object
class, palettes/colors, character level/skills/free slots, calculated damage,
innate-cantrip-adjusted properties and rating totals. Retired type 6 always fails.
Use the importer or Loot Profile Editor to construct these rather than guessing
argument order. See [Classic compatibility](VirindiCompatibility.md#loot-classification-and-actions).

Native profile validation supports at most 2,048 loot rules, 64 conditions per
rule, and 256 UTF-8 bytes for rule names/labels. It rejects enabled rules with
unresolved `compatibility_issues`. Calling `lootmatch` directly is **not** a
replacement for this validation: it expects correctly shaped rule and item
tables, and does not enforce import compatibility, retention, trade restrictions,
capacity, wield requirements, or salvage eligibility. Check action-specific
snapshot flags and server results separately before consuming or moving items.

## Minimal companion preview

This example previews one fixed rule against the selected owned item. It requests
no inventory actions and prints no match notifications. Use a manifest with
`api: 2`, `mode: "companion"`, permissions `data` and `ui`, and
`snapshot_fields: ["selected","inventory","inventory_revision","appraisal_revision"]`.
The normal required fields, including player and server, remain present.

```lua
local rule = {conditions={{field="int",key=218103840,op="in",values={45,46}}}}
local scan, token = {}, nil

return function(s, p)
  local item
  for _, candidate in ipairs(s.inventory or {}) do
    if candidate.id == s.selected then item = candidate; break end
  end
  if not item then
    scan, token = {}, nil
    return {ui={{id="match",text="Select an inventory item"}}}
  end

  local current = table.concat({s.server or "", tostring(s.player),
    tostring(item.id), tostring(s.inventory_revision), tostring(s.appraisal_revision)}, ":")
  if token ~= current then scan, token = {}, current end
  local result = lootmatch(rule, item, s, scan)
  local text
  if result == "pending" then
    text = "Checking..." -- Keep scan and resume on the next callback.
  else
    scan = {}
    if result == nil then text = "Assess this item to check the rule"
    elseif result == true then text = "Matches Light or Finesse Weapons"
    else text = "Does not match" end
  end
  return {ui={{id="match",text=text}}}
end
```

For automatic assessment, add the `identify` permission and submit
`{action="identify", target=item.id}` through the companion action API, respecting
its shared one-per-second command limit and waiting for appraisal changes. An
automation driver uses `inventory` permission for appraisal. Neither a match nor
a request being accepted proves that a subsequent pickup or salvage succeeded.


## Simple quality goals (local v169)

The main UCM Loot tab defaults to explicit pickup goals. Choose a minimum
quality, one or more categories, and optionally a base-skill wield requirement:
Any, No skill requirement, Exactly, or Between (inclusive). Examples are
Uncommon+ armor/jewelry, Legendary wands with no skill wield, and Legendary
wands requiring exactly 270. Goals use the hover evaluator, including explicitly
estimated tinkered scores. They do not penalize good future equipment just
because the current character cannot wield it. Arcane Lore and level gates are
separate from the skill-wield filter. Build-complement jewelry selection is not
part of this feature.

Native JSON fields: `quick_goal` boolean, `min_quality` integer 0..5,
`item_categories` nonempty string array, `wield_mode` any/none/exact/range,
`wield_min` and `wield_max` nonnegative integers. Advanced rules can set
`allow_estimated: false`. Categories are weapons, melee, missile, heavy, light,
finesse, twohand, bow, crossbow, atlatl, wand, armor, shield, jewelry, clothing,
essence. Parent categories include their children. Missing identification waits
for appraisal; identified items without a usable grade do not match. An absent
skill requirement is zero, independently of Arcane Lore.

Adding a goal puts it first in the existing first-match rule order. Editing,
toggling and deleting save through the normal profile flow and stop UCM until
restarted. Opening the panel does not alter profiles. Advanced rules remain
available. New goals contain a retired always-false Classic condition (legacy
key 6), ignored only by the new quality-goal matcher. This prevents an older
client from ignoring unknown quality fields and accidentally keeping all loot.
Quality goals are a native-profile feature; original-format UTL cannot express
them and retains that closed compatibility guard.


## Recipient-aware quality goals (local v172)

Use **Rate for** in the simple Loot goals: Current character, Universal / trade
loot, or any combination of preset and named templates. Universal evaluates
whole coherent combat/crafting templates plus saved custom templates and keeps
the best full score. It does not combine incompatible spells across recipients.
Named templates can start from a preset or copy the current character's trained
skills, then be customized and saved for a friend/alt. They live in the shared
Loot profile and travel with that profile when selected on another character.

Several selected templates mean **any one qualifies**, not all must qualify.
Quality, item category and wield-bracket filters still combine normally.
Usable-now/close filters apply only to Current character goals. Recipient goals
ignore the looter's own skill limits. Personal hover/icon ratings remain personal;
pickup explanations name the recipient whose quality threshold was met.
Existing goals default to Current character and retain their prior behavior.
