# Creature reference catalog

`creatures.json` is generated from the public ACE retail world release v0.9.296:
https://github.com/ACEmulator/ACE-World-16PY-Patches/releases/tag/v0.9.296

SQL SHA-256: `7f58832a326526f72321d3ce0ddf463fe80cef2b12a08090dbdd03964ba021b4`.
This is reference content, not a Coldeve database export or live observations.
Spell names and descriptions are resolved from the player's installed DATs;
no retail artwork or DAT copies are included here.

Rebuild with `tools/atlas/import_ace_world.py` (including body-part and spell-book
tables), then `export_creature_knowledge.py`. The exporter refuses an existing
output path; use a new output and replace this generated file after review.
Native reader: `Knowledge/ACEWorldKnowledge.h`; shared presentation:
`Private/UI/ACEAppraisalKnowledge.cpp`. Knowledge is keyed by template ID, never
name, and can later be shared with navigation/macros through the plugin API.

Melee types come from damaging body parts. Spell lists are known repertoire,
not observed cast frequencies. Explicit resistance values are damage-taken
multipliers, not resistance percentages or a complete weapon recommendation;
missing properties stay unknown, and live appraisal values take precedence.

Drop entries cover direct creature death create-lists, filtered by ACE's
Creature_Death.cs rules. Standard-setting chances mirror the ordered float32
selection in Creature_Equipment.cs, including mutually exclusive choices, empty
outcomes, cumulative set boundaries and repeated-item union probability. They
assume trophy_drop_rate=1. Displayed percentages are rounded baseline values,
not exact assurances for a specific server. Scripted drops, procedural treasure,
events, server changes and runtime conditions are not fully covered.

`locations.json` is generated with `tools/atlas/export_locations.py` from the same
public ACE World v0.9.296 SQLite import. It includes 382 outdoor portal entrances,
vendors and lifestones. Coordinates describe the placed entrance, not the portal's
exit. Towns come from the existing retail map. Indoor-only portals and dynamically
generated placements are not included. This is a baseline, not a Coldeve export.
Waypoint uses this catalog automatically when no user-imported atlas exists.
Search results set the map/arrow destination; they do not start automatic travel.
