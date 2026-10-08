# Validation — 2026-10-08

Windows x64, MSVC 19.38, Qt 6.8.3 and Python 3.12.14.

- C++20 Release build succeeds. Compiler warnings are limited to Qt QWidget's
  internal `data` member being shadowed by local response variable names.
- Eight replay-independent backend contract tests pass: unknown/empty player
  selection; null/sentinel handling; owner-separated episodes and exact evidence;
  actor/target/type/time/literal-text filters; bounded pages and unavailable
  simulation metrics; complete filtered JSON/CSV exports with selection provenance;
  prompt protected-destination errors; damaged replay rejection without published cache.
- CTest native GUI construction/render test passes.
- Native asynchronous model smoke test on a decoded replay loads 834,607 events,
  33,479 episodes and 7,302 findings, with bounded 250-row pages. The rendered
  window has readable text and visible player/header/filter/evidence controls.
- A second real replay decodes 23 events and correctly identifies a human and a
  computer from distinct header types. No slot-to-color or slot-to-AI assumption.
- All **766,932** event records in the existing exact legacy extraction match
  the new index byte-for-byte after JSON decoding, including original sequence,
  millisecond timestamps, offsets, fields and retained raw packet hex. The new
  index also includes packet categories that the legacy extractor omitted.
- Full build: **57.656 s**, peak Python working set **132,104,192 bytes** (about
  126 MiB), SQLite index **462,807,040 bytes**. Sample actor query: **0.102 s**
  including process startup. These are measurements from this replay/machine,
  not universal performance guarantees.
- Cancellation during decoding leaves only an ineligible partial file. No
  complete SQLite cache was published. The next unchanged full build is a
  verified cache hit. Source replay SHA-256:
  `1f9fa112050090de1e0a33bf7885ad41229f5da3cb5111eb4d684eb4069f9446`.
  Verified pipeline SHA-256:
  `b74c0a41cbc2380800af91f156231a156a803c13f94cd27c70d138e35f1752cc`.
- Packaged native smoke test passes with SDK paths removed from PATH, including
  paging, player selection and exact episode evidence navigation. Bundled backend
  modules resolve beside the installed executable. The package includes Qt's
  runtime plugins and the compiler runtime; it still requires Python 3.12+.
- The difficulty-byte interpretation for DE save versions >=61.5 is flagged
  explicitly uncertain, matching the decoder's own source comment. Sync-derived
  guessed simulation counters are not exposed as observed statistics.

The local replay files remain outside this project and are not distributed.
Generic fixtures use arbitrary players, actor IDs and custom type IDs. Available
real replays are from the user's modded sessions; a real vanilla/default-AI match
and Linux execution remain unverified. The application contains no required mod
installation or AI profile. Only decoded DE `.aoe2record` support is enabled.

The supplied `tests/validate_live.py` can certify a real replay, cache reuse,
cancellation and equality with a saved legacy event stream. It streams the legacy
JSON rather than importing that report into the GUI. Machine-specific results
belong under the ignored `validation/` directory or an external analysis folder.

Remaining practical limits: disk indices can be substantially larger than replay
files; literal full-payload text searches and deep OFFSET pages can be slow, but
run in cancellable worker processes. Indexing/reconstruction progress reports a
phase rather than an invented precise ETA. Million-event behavior is supported
by the storage/model architecture; this session's real replay is below one million.
Missing object state, successful execution and internal AI intent remain explicit
limitations, not metrics synthesized by the frontend.


## Standalone migration — 2026-10-08

The existing GitHub main history and root GPLv3 license were retained. Original
MIT notices for the reused helper modules are separate. Bundled decoder/helper
bytes and the verified pipeline SHA-256 above are unchanged after relocation.
Git attributes preserve those bytes across checkouts.

A fresh Windows Release build, all eight backend tests and the CTest native
check passed in the independent project. The installed executable passed with
Qt SDK directories absent from PATH: it decoded the 23-event replay, then opened
the certified 834,607-event index and checked native paging, player filtering
and exact episode evidence navigation. Its backend roots resolve beside the
installed executable; its binary contains no original development path. The
installed GPLv3 license matches the repository file exactly.

Historical evidence is retained under ignored `validation/migration-origin/`;
new migration logs, a rendered GUI image and results are under
`validation/migration-checks/`. These local artifacts, Qt SDK and generated
packages are excluded from Git. Earlier large-replay measurements above remain
measurements of the original decoding run.
