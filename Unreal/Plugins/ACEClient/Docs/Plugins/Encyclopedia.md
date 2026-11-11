# In-game encyclopedia proof of concept

Feature branch: `codex/ingame-wiki-proof-of-concept`.

Enable **ACPedia** in the plugin manager (enabled by default on first discovery).
Examine an object with a matching ACPedia article, then click **ACPedia →** in the
description footer of Examine. The link appears after the site confirms an exact title or
redirect match. Player characters are excluded. Missing/unavailable matches stay
hidden; transient lookup failures retry after one minute.

The separate native window keeps its current article as Examine changes. Use
**Related entries**, **Back**, or **Forward** to navigate. You can also type an exact
article title and press Enter/Open. Opening ACPedia from the plugin bar works
without a selected object. Disabled plugins do not offer the Examine link.

Title input supports debounced partial-name and wildcard suggestions, canonical
case-insensitive title resolution, cancellation of stale requests, and Escape
to dismiss suggestions. This is title search, rather than full-text article search.

This preview reads `https://acpedia.celestro.com/api.php` over HTTPS. It shows text,
headings, lists, flattened table values, and related article text links. It does not
embed a browser, execute HTML/scripts, load external article links, or issue game
commands. Only requested article titles/object names are sent; no credentials or
character statistics are used. A title match is a reference link, not authoritative
proof that a server object has retail properties.

Each article includes its source URL and revision. Network failures offer Retry.
Requests time out, response/parser size limits are enforced, stale article replies
cannot replace a newer selection, and session history is limited to 64 entries.
Lookup cache is bounded and concurrent lookup requests are limited to four.

Reader controls: saved 8–32 point text size (20 default), distinct gold/bold
headings, and inline PNG/JPEG images hosted under the restored site's `/images/`
path. Image loading is bounded to eight per article, eight MiB compressed and four
million decoded pixels per image. Missing files show an unavailable label. Adjacent
article images share horizontal rows and wrap when the window is narrow.
Web edit links, contents/navigation chrome, placeholder spawn-map layers and
release-history boilerplate are filtered from the game view; guide content stays.

Creature attributes are grouped into a compact, aligned label/value table under
an expandable Attributes heading. Opening from a successful Examine identification
collapses that reference section by default; unsuccessful or unknown identification
keeps it expanded. Manual expansion is remembered for the article during the session.
These are wiki reference values, not live appraisal results. Image Needed artwork
is omitted.

Not included yet: faithful complex table/template layout, full-text search,
bookmarks, persistent history, hybrid Fandom content, structured quest guides,
markers, loot scoring, or any automatic quest execution. In-game visual testing
still requires launching this preview and examining an object while logged in.

Examine uses consistent 10-pixel content margins and prioritizes Vulnerabilities
(all known multipliers, highest first, with visible percentages),
then Common Attacks grouped as Melee, Magic and Missile. Beneficial/support spells
are omitted from the Magic group when metadata identifies them. Unknown missile
data remains unknown; melee damage is not used as a substitute. Combat icons are
28 native layout pixels, with visible labels. Related wiki text links follow the chosen reader text size.

Focused automation: `ACE.Wiki.NativeReference`. Existing related checks:
`ACE.Inspection.Knowledge` and `ACE.RetailParity.UIInteractions`.

Game UI typography is separate from chat: Options → Config → User Interface
Options provides Game UI Font (Classic serif, Sans serif, Monospace) and Game UI Text
Size (Tiny through Extra Large). Apply updates existing retail text labels,
including skills and smooth Examine text. Chat and authored game tooltips retain
their own font behavior. Plugin tooltips use a separate cursor-clearance adjustment.

Creature attributes use left-aligned names, right-aligned values, and a centered portrait. A real article portrait takes priority. If absent or unavailable, opening the entry from Examine captures that creature locally; the image stays with the article when Examine changes. Up to 16 portraits are retained for the session. Manual searches can reuse an already captured portrait, but cannot invent an appearance for an unseen creature. No captured image is uploaded. Generic Live.jpg and Image Needed placeholders and spacer artwork are omitted.

The **Use for Examine** checkbox is optional and off by default. When enabled, a completed Examine request opens the exact matching article and closes the stock window after the match is confirmed. Missing articles, player characters, and spells retain stock Examine. Browsing history never follows ordinary target selection.

Creature infoboxes show a compact type/level header, a reward summary, labelled trophies, separate attacks and vulnerabilities, and an attack/defense ratings table. Known damage multipliers are displayed as percentages, highest first (100% means normal damage taken before armor and other modifiers). Appraised values take priority over reference values. A wiki weakness list without numeric data stays labelled as such. Unknown multipliers are never invented. Combat icons in stock Examine are smaller and have visible names and percentages.


Local implementation checkpoint, 2026-10-29: native gameplay panels, controls,
and pointer transforms now scale with Game UI Text Size. Settings rows can grow
for wrapped labels. Character selection retains its authored font placement;
reader text size remains a separate saved setting. The UCM mini remote pins its
quick checkboxes to a horizontal footer and reserves compact height for all six
primary activities. Targeted UIInteractions and Ledgers checks pass.

The larger product brief remains a roadmap: persistent quest tracking, hunt mode
with event-based steps, quest/loot markers, item scoring and hybrid Fandom data
are not delivered by this preview. Automatic quest execution is on hold and
dungeon route generation is shelved.
