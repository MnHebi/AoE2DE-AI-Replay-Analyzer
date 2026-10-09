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

## Windows release v0.1.0 — 2026-10-08

- `cmake --build build --config Release` passed with MSVC and the configured
  Qt 6.8.3 SDK. The Windows SDK registry requires a build outside the restricted
  sandbox; the initial restricted attempt failed before compiling.
- Python 3.12.14 ran `-m unittest discover -s tests -p test_adapter.py -v`:
  all eight backend contract tests passed. `ctest --test-dir build -C Release
  --output-on-failure` passed its native smoke test.
- Installed into a fresh `dist/` directory. With PATH limited to Windows system
  directories and Python selected explicitly, the installed executable decoded
  the 23-event replay into a new local cache, then opened the existing
  834,607-event index. Both native smoke tests passed their applicable paging,
  player filtering and exact episode evidence checks and saved screenshots.
- Every bundled backend file matches its repository source bytes, the installed
  GPLv3 license matches exactly, and the decoder pipeline remains
  `b74c0a41cbc2380800af91f156231a156a803c13f94cd27c70d138e35f1752cc`.
- ZIP integrity and all 165 payload SHA-256 hashes in `CERTIFICATION.json`
  passed. All 142 tracked source files match the archive byte-for-byte. Its
  membership contains only those sources, the installed runtime files and the
  certification manifest; no replays, caches or local evidence are included.
- The executable extracted from the ZIP passed both native smoke checks with
  SDK directories absent from PATH. Its new small-replay cache confirms parser
  and helper roots inside the extracted package. `SHA256SUMS.txt` records the
  complete ZIP hash for release download verification.

Local release evidence is under ignored `validation/release-v0.1.0/`. Replays,
indices and screenshots are excluded from release payloads. Python is not
bundled; these checks used an explicit Python 3.12.14 installation. They do not
extend the previously stated replay-format, vanilla/default-AI or Linux coverage.

## Launch, navigation and review fixes — v0.1.1, 2026-10-08

- Windows Release build passed. All nine backend contract tests passed, including
  a dense, filtered `anchor_ms` page with exact centering, end clamping and an
  empty selection. Decoding and the SQLite schema remain unchanged.
- CTest passed `native-regressions` and `native-smoke`. Seven native cases cover
  rejecting Store/old-runtime candidates and actionable missing-Python errors;
  direct EXE discovery without an override; page 3 retained across tabs with only
  visible queries logged; a real chart click near 80% of an 800-event fixture
  selecting the nearest time on a bounded page while retaining chart context;
  invalid/overflow numeric filters blocking queries; and distinct unknown/zero
  owners, same-filename label isolation and unavailable/different context fields.
- Installed into a fresh staging directory with SDK directories absent from
  PATH and `AOE2_PYTHON`/`AOE2_BACKEND_ROOT` unset. The installed executable
  discovered Python and decoded the 23-event replay. Its indexed players, events,
  actor membership, episodes, evidence membership and diagnostics match v0.1.0
  exactly. It also opened the existing 834,607-event index. Both smoke checks
  exercise paging, player selection, exact evidence and an actual timeline
  mouse event, validate the returned time window and nearest selected event,
  and save a rendered screenshot. The large index was reused, not rebuilt.
- Bundled backend bytes match their sources and the installed GPLv3 license
  matches exactly. UTF-8 and UTF-16 binary scans find no current development
  directory or original AI project directory. Backend roots resolve within the
  installed package, with no development-path fallback.
- New pipeline SHA-256:
  `d5755213a944e52e46ed67cc6008da88396d42622ce975487f93e769c1bb35ab`.
  The change comes from the adapter's additive navigation query; vendor parser
  and helper bytes are unchanged. Older indices retain their original pipeline
  hashes, and fresh builds use the new cache identity.
- Release ZIP integrity, all 169 certified payload hashes and all 146 source
  files passed verification against the committed source snapshot. The manifest
  records its source commit. No replay, cache, SDK or local evidence is included.
  The extracted EXE passed the same two smoke checks with Python/backend
  overrides unset and SDK paths removed. `SHA256SUMS.txt` records the ZIP digest.

Local evidence is under ignored `validation/release-v0.1.1/`. The first native
test attempt could not create SQLite data in the default temporary location;
fixtures now use the ignored build directory. The original `dist/` was locked,
so installation uses a separate stage. These checks establish the tested GUI
paths and package behavior; they do not establish gameplay outcomes, Linux
execution or real vanilla/default-AI coverage. Python 3.12+ remains external.

## Terms and guide — v0.1.2, 2026-10-08

- Windows Release build passed. Existing `QWidget::data` shadowing warnings
  remain in window.cpp; no new help-source warning was reported.
- CTest passed all three targets: `native-regressions`, `native-smoke` and
  `native-help-smoke`. Eight native regression cases now include F1 access
  before loading data, case-insensitive title/description search, multiple
  search words, an empty result, keyboard topic navigation, Escape and reopening
  the same window. Opening/searching help over a loaded fixture preserved the
  current filters and page and emitted no additional backend query.
- The 67 compiled topics were checked against the current adapter/contract,
  including the distinction between an episode's neighboring gaps of at most
  10 seconds and a repeated-command diagnostic's entire span of at most 10
  seconds. Gap evidence endpoints and exact episode membership are explained
  separately, along with unresolved outcomes and unavailable simulation state.
- Fresh installation rendered the help window with PATH limited to system
  directories and Python/backend overrides pointing to nonexistent paths. Its
  saved start-page image was visually inspected for legibility, layout, topic
  navigation and search controls. Help performs no decoding.
- The installed EXE also passed native paging, player selection, exact evidence
  and timeline navigation checks on the 23-event replay and existing
  834,607-event index, with Python/backend overrides unset and SDK paths absent.
  The small replay's indexed records still match v0.1.0 exactly. The large index
  was reused, not rebuilt.
- Bundled backend bytes and GPLv3 license match the sources. The executable has
  no current or original development path. Parser/helper roots resolve within
  the installed package. The pipeline remains
  `d5755213a944e52e46ed67cc6008da88396d42622ce975487f93e769c1bb35ab`.

- Release ZIP integrity, all 171 certified payload hashes and all 148 committed
  source files passed exact byte verification. No replay, cache, SDK or local
  evidence is included. The extracted EXE rendered help with missing interpreter
  and backend overrides, then passed the same two replay checks with SDK paths
  absent. `SHA256SUMS.txt` records the complete archive digest.

Local evidence is under ignored `validation/release-v0.1.2/`. These checks cover
the new help and existing tested native paths. Replay-format, gameplay-outcome,
Linux and real vanilla/default-AI coverage limits remain unchanged; replay
analysis still requires external Python 3.12+.

## RESEARCH packet decoding correction — 2026-10-09

- Confirmed the exact supplied 16-byte outer payload fails the old parser when
  it attempts to read a missing four-byte building ID after the 13-byte header.
  The new isolated test reproduced that failure before the patch. Upstream
  PR #149 was inspected through GitHub CLI: closed, unmerged; its length-based
  rule matches this fork, but its `ValueError` would stop this adapter's stream.
- Two bundled `mgz/fast` files were changed. RESEARCH accepts the 13-byte header
  or an exact header plus `4 * selected` bytes, rejecting negative selection
  counts and unsupported sizes through `struct.error`. List-bearing packets
  preserve the established decoded fields. The framed-action error path retains
  original action ID, declared payload length excluding the action-ID byte,
  full payload bytes, error text and wire sequence instead of returning an empty
  ERROR record. Successful research also retains full payload bytes. Other
  parser/helper files and dependency metadata retain their bytes; license
  notices and pre-existing vendor line endings are preserved.
- Python 3.12.14 ran `-m unittest discover -s tests -p test_adapter.py -v`:
  all 15 tests passed. Six new cases cover the exact packet (player 1, actor
  7698, technology 22), one-/two-ID list-bearing packets, legacy research,
  unrelated valid actions, truncated/extra-byte/negative-count layouts,
  unresolved evidence and continuation into a valid operation. A body fixture
  checks timestamps, operation sequence, offsets, SQLite evidence diagnostics,
  observed research status, and CLI cache reuse. Its header/preamble are supplied
  fixtures, not evidence of additional supported replay formats.
- Initial new-test setup failures were corrected by explicitly loading the
  bundled helper path and closing the fixture's SQLite handle before Windows
  temporary-directory cleanup. These were fixture issues; the final full
  focused suite passed.
- The available large replay has SHA-256
  `1f9fa112050090de1e0a33bf7885ad41229f5da3cb5111eb4d684eb4069f9446`.
  Direct byte inspection at all 531 old ERROR offsets confirmed action ID 101
  and 16-byte payloads. One controlled full rebuild took 55.456 seconds and
  published a fresh index in a separate cache. Actual results:

  | Metric | Before | After |
  | --- | ---: | ---: |
  | Recorded events | 834,607 | 834,607 |
  | RESEARCH actions | 0 | 531 |
  | ERROR / unresolved packets | 531 | 0 |
  | Diagnostic findings | 7,302 | 6,771 |
  | Repeated-command findings | 6,771 | 6,771 |
  | Episodes | 33,479 | 33,480 |

- All 834,076 unaffected event rows compare exactly, including decoded JSON,
  indexed fields and signatures. Every event retains its ID, original operation
  sequence, timestamp and replay offset. Each restored record was checked
  against its source packet for player, building, technology and full bytes.
  Player metadata and replay settings/duration remain equal. No research packet
  remains unresolved in this replay. Commands are requests; no research
  completion or AI intent was inferred.
- The source fingerprint changed from
  `d5755213a944e52e46ed67cc6008da88396d42622ce975487f93e769c1bb35ab` to
  `f9d74470c5f51afe124eef92711176cdde34fb00fc898ccdf7f98b26426e28cd`.
  The first invocation reported a cache miss; a second invocation reported a
  hit on that same new index without decoding again. Schema remains v1.
  Complete SHA-256 comparisons confirm the baseline index and original replay
  are unchanged. Older indices remain readable with their original provenance.
- `ctest --test-dir build -C Release --output-on-failure` passed all three
  targets. The existing Release executable, using the changed source backend,
  opened the corrected large index and passed native paging, player selection,
  exact episode evidence and timeline navigation, with 531 restored research
  requests and 6,771 findings. No native source or release asset was changed.

Local scripts, baseline packet evidence, build log, corrected index, results and
GUI screenshot are under ignored `validation/research-fix/`. Unknown inner
layouts remain unresolved; damaged outer operation framing may still stop
decoding. This verification covers the available replay and deterministic packet
fixtures, not all game versions or actual standard/default-AI and human replay
sources. The fix is independent of player identity and custom AI/game data.

## Windows release v0.1.3 — 2026-10-09

- `cmake --build build --config Release` passed with the configured MSVC/Qt
  toolchain. CTest passed all three targets after the release version update.
  The preceding correction section records the 15 passing backend tests and
  single controlled large-replay rebuild; that large replay was not rebuilt
  again for packaging.
- Fresh installation under ignored `build/release-v0.1.3-install/` passed
  isolated checks using its own bundled parser: the exact header-only packet,
  two-ID list-bearing research, and a malformed packet retaining its original
  ID, length, raw bytes and error while a following valid packet still decodes.
- Installed help rendered with nonexistent Python/backend overrides. With SDK
  paths absent and Python/backend overrides unset, the installed EXE decoded
  the 23-event replay and opened the corrected 834,607-event index. Both passed
  native paging, player selection, exact evidence and timeline navigation. The
  large index retains 531 RESEARCH requests, zero unresolved packets and 6,771
  findings. The small replay's rows still match the original baseline exactly.
- Every installed backend file matches the current source bytes, including the
  two corrected parser modules; the GPLv3 license matches. The EXE contains no
  current or original development path, and parser/helper roots resolve inside
  the package. The pipeline fingerprint is
  `f9d74470c5f51afe124eef92711176cdde34fb00fc898ccdf7f98b26426e28cd`.

- ZIP integrity, all 171 certified payload hashes and all 148 committed-source
  byte comparisons passed. The extracted package passed its own research packet
  checks, offline help rendering, small-replay decoding and native checks on the
  corrected large index with SDK paths absent. No replay, cache, SDK or local
  evidence is included. SHA256SUMS.txt records the complete archive digest.

Local release logs and checks are under ignored `validation/release-v0.1.3/`.
Reopening the source replay selects the corrected cache identity; opening an old
index preserves that index's original data and provenance. Python 3.12+ remains
external. Existing replay-format, gameplay-outcome, Linux and real
vanilla/default-AI/human replay coverage limits still apply.
