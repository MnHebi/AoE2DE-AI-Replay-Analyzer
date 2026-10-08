# Replay adapter contract v1

`backend/adapter.py` is a CLI boundary independent of the GUI. Build emits one
JSON object per stdout line: progress (`phase`, `percent`, optional `events`),
ready (`database`, `cache_hit`) or error (`message`, nonzero exit). Query and
export emit exactly one JSON object. Stdout is never parsed as prose.

```
python adapter.py build replay.aoe2record --cache-dir CACHE [--parser-root PACKAGES] [--tools-root HELPERS]
python adapter.py query index.sqlite --request '{"view":"events","filters":{"players":[2],"actor_id":400},"offset":0,"limit":250}'
python adapter.py export index.sqlite results.json --format json --request '{"view":"events","filters":{}}'
```

Requests support `overview`, `events`, `episodes`, `diagnostics`, `statistics`
and `timeline`. Table responses have `rows`, `total`, `offset`, `limit` (maximum
500). Event filters: `players`, `include_unknown`, `from_ms`, `to_ms`, `action`,
`category`, `status`, `actor_id`, `target_id`, `type_id`, literal `search`, `id`,
`ids`, `episode_id`. An absent `players` key includes every owner; an empty
array includes no known owner. Episode/diagnostic filters use players, overlapping
time range and status. Event-specific filters do not alter episode reconstruction.
SQL values are bound parameters; search escapes LIKE wildcards.

SQLite is the indexed representation. Guaranteed tables:

| Table | Contents |
| --- | --- |
| metadata | JSON values for schema, replay metadata, coverage, warnings, limitations, provenance and completion |
| players | Header number and original metadata JSON; selected color validated by the existing header helper |
| events | Stable index ID, exact game milliseconds, original operation sequence, byte offset, action, category, evidence status, optional player/target/type, actors, original decoded JSON |
| event_actors | Many-to-many exact actor/event membership |
| episodes | Consecutive identical command runs per actor and known/unknown player, bounds, supporting count, inferred status, unresolved outcome |
| episode_events | Exact evidence membership, not just an enclosing time range |
| diagnostics | Generic rule description, uncertainty, status, bounds, evidence endpoints, optional episode link |

Unknown owner, target, type and settings stay null. No ownership or identity is
guessed from color, slot, chat, command type or actor ID. Original absent-target
sentinels remain in `raw_json`; indexed target filters treat them as null.
An object table is not fabricated: event_actors records references without
claiming object simulation metadata. Type IDs describe explicit packet requests,
not the actor's own unit type.

Status has four meanings: `observed` decoded fields, `inferred` interpretation,
`unresolved` undecoded or uncertain outcome, `unavailable` missing information
(represented by null values, not fabricated rows or zero metrics). Corrections
fail closed with original packet bytes retained; potentially corrupt fallback
actor IDs are discarded. The frontend exposes fields independently of status.
Repeated-command identity is based on decoded fields, with selection membership
removed for per-actor analysis. Unexposed packet bytes may differ; this is not a
claim of complete binary packet equivalence or an internal AI loop.

Cache identity is SHA-256 of the complete replay plus SHA-256 over adapter,
parser source/reference files and helper sources, with the schema version. A
temporary `.partial` file is atomically renamed only after indexing completes.
A complete index may represent a **partial replay decode**; coverage says so.
Cancellation never turns a `.partial` into a reusable cache. Interrupted partials
may remain and can be removed manually when no analysis is running.

Decoder fingerprint and original paths are available locally and in exports.
Generic profiles and user labels are **session metadata**, excluded from raw
observations. Exports carry those labels, filters, filename, replay hash and
pipeline fingerprint. JSON/CSV stream all selected rows with bounded Python
memory; text summaries contain metadata and observed counts. Another analyzer
can implement this contract without changing the native tables.
