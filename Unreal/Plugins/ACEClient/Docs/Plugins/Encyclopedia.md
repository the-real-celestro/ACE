# In-game encyclopedia proof of concept

Feature branch: `codex/ingame-wiki-proof-of-concept`.

Enable **Encyclopedia** in the plugin manager (enabled by default on first discovery).
Examine an object with a matching ACPedia article, then click **Wiki →** in the
upper-left of Examine. The link appears after the site confirms an exact title or
redirect match. Player characters are excluded. Missing/unavailable matches stay
hidden; transient lookup failures retry after one minute.

The separate native window keeps its current article as Examine changes. Use
**Related entries**, **Back**, or **Forward** to navigate. You can also type an exact
article title and press Enter/Open. Opening Encyclopedia from the plugin bar works
without a selected object. Disabled plugins do not offer the Examine link.

This preview reads `https://acpedia.celestro.com/api.php` over HTTPS. It shows text,
headings, lists, flattened table values, and related article buttons. It does not
embed a browser, execute HTML/scripts, load external article links, or issue game
commands. Only requested article titles/object names are sent; no credentials or
character statistics are used. A title match is a reference link, not authoritative
proof that a server object has retail properties.

Each article includes its source URL and revision. Network failures offer Retry.
Requests time out, response/parser size limits are enforced, stale article replies
cannot replace a newer selection, and session history is limited to 64 entries.
Lookup cache is bounded and concurrent lookup requests are limited to four.

Not included yet: images, faithful complex table/template layout, full-text search,
bookmarks, persistent history, hybrid Fandom content, structured quest guides,
markers, loot scoring, or any automatic quest execution. In-game visual testing
still requires launching this preview and examining an object while logged in.

Focused automation: `ACE.Wiki.NativeReference`. Existing related checks:
`ACE.Inspection.Knowledge` and `ACE.RetailParity.UIInteractions`.
