# Project handoff

Independent repository: https://github.com/MnHebi/AoE2DE-AI-Replay-Analyzer.
The initial GPLv3 license is retained. The application and all decoder/helper
sources live here; no Rome at War checkout is needed to build or run it.

Implemented: native overview, player selection, paged events and raw evidence,
timeline, inferred command episodes, generic diagnostics, player statistics,
comparison, optional labels/profiles, exports and cancellable SQLite indexing.
Commands do not establish successful simulation outcomes. `CONTRACT.md` defines
the backend contract and evidence limits.

Windows x64 is verified with MSVC 19.38, Qt 6.8.3 and Python 3.12.14. See
`VALIDATION.md` for exact replay identity, equality checks and measurements.
Actual vanilla/default-AI replay execution and Linux remain unverified; other
record formats are disabled. These are coverage gaps, not gameplay verdicts.

Generated artifacts stay ignored: `build*/`, `dist/`, the Windows release ZIP,
`.tools/` and `validation/`. The local Qt SDK is under `.tools/qt-sdk`; callers
can use any compatible Qt 6 installation. Local migration evidence is under
`validation/`. Usage, build commands and Python configuration are in `README.md`.

Release v0.1.0 packages Windows x64 in `AoE2ReplayAnalysis-Windows-x64.zip`,
with Qt/compiler runtimes, the unchanged decoder/helpers, rebuildable sources,
license notices and a per-file `CERTIFICATION.json`. Python 3.12+ is an external
requirement. GitHub release assets include `SHA256SUMS.txt`; generated archives
and local release evidence under `validation/release-v0.1.0/` remain ignored.
Release build, eight backend tests, CTest and installed native checks passed;
the installed checks decoded the 23-event replay and opened the 834,607-event
index with Qt SDK directories absent from PATH. Existing format/runtime limits
still apply. Use `tests/package_release.py` after a fresh Release installation
to produce the ZIP. Archive integrity, per-file SHA-256 hashes and source byte
equality passed; the extracted executable also passed both native checks and
resolved its decoder/helpers within the extracted package.

Release v0.1.1 fixes direct EXE Python discovery and timeline navigation, plus
the v0.1.0 review findings. Python candidates are version-probed asynchronously;
Store aliases and old runtimes are rejected. Timeline clicks use a bounded
`anchor_ms` events request, keep the chart context and select the nearest event.
Views load on demand, preserve pages across tabs and reuse identical requests.
Comparison labels use replay hashes, owner 0 is distinct from missing ownership,
context fields retain unavailable states, and invalid numeric filters block
queries/exports visibly. The native executable no longer embeds a source fallback.

Nine backend tests and seven native regression cases cover these changes, in
addition to the GUI construction test and installed real-replay smoke checks.
The adapter query extension changes the pipeline fingerprint to
`d5755213a944e52e46ed67cc6008da88396d42622ce975487f93e769c1bb35ab`;
bundled decoder/helper bytes remain unchanged. Existing indices keep their
original provenance. See the v0.1.1 section in `VALIDATION.md` for check limits.
The release stage is under ignored `build/release-v0.1.1-install/` because the
existing local `dist/` was locked. The packager accepts `--dist PATH` and bundles
source bytes from the committed revision, recorded in `CERTIFICATION.json`,
so unrelated local edits are excluded. Local release evidence remains ignored.
The v0.1.1 ZIP passed integrity, all 169 payload hashes, all 146 committed-source
byte comparisons and both extracted-EXE smoke checks. Publish its companion
`SHA256SUMS.txt` with the ZIP; neither generated asset belongs in source control.

Release v0.1.2 adds Help > Terms and guide (F1), a modeless searchable glossary
with 67 topics. It explains views, episodes and diagnostic thresholds, evidence
levels, IDs, command categories, filters, comparison context, metadata and cache
behavior. Definitions are compiled into the EXE and require neither Python nor
a loaded replay. Search matches titles and descriptions, prioritizes matching
titles, and reports no matches clearly. The window can remain open beside the
replay without changing its selection or page. `--guide` also opens it directly.

Release build and all three CTest targets passed, including eight regression
cases and a standalone help render. Installed checks rendered help with missing
Python/backend overrides, then exercised the small replay and existing large
index with SDK paths absent. Backend bytes and the v0.1.1 pipeline fingerprint
are unchanged. See VALIDATION.md; local evidence and the fresh install stage are
under ignored `validation/release-v0.1.2/` and `build/release-v0.1.2-install/`.
The ZIP passed all 171 payload hashes and all 148 committed-source byte checks;
the extracted EXE passed help rendering and both native replay smoke checks.

The 2026-10-09 RESEARCH decoder correction is a local patch to two bundled
`mgz/fast` files, documented in THIRD_PARTY.md. Header-only 13-byte inner packets
and exact `13 + 4 * selected` packets decode by validated length, without player
or AI assumptions. Unsupported inner layouts use the existing `struct.error`
path and retain original action ID, payload length, full bytes, parser error and
sequence as unresolved evidence; subsequent framed operations remain readable.
Successful RESEARCH packets retain full payload bytes as well. Schema v1 remains
unchanged. Outer framing corruption can still stop decoding, without resync.

All 15 backend tests and the three existing CTest targets passed. The large
replay was rebuilt once into a separate cache: 834,607 events stayed unchanged,
RESEARCH rose from 0 to 531 and unresolved packets fell from 531 to 0. All
834,076 other event rows are identical, and every event's ID, timestamp, sequence
and offset is unchanged. Diagnostics fell from 7,302 to 6,771; repeated-command
findings remain 6,771. The existing Release GUI passed its native smoke paths
against the corrected index. Old indices and source replay remain untouched.

The new pipeline fingerprint is
`f9d74470c5f51afe124eef92711176cdde34fb00fc898ccdf7f98b26426e28cd`.
Local baseline, controlled-build log, comparison results and corrected index are
under ignored `validation/research-fix/`. Future builds use this new cache
identity; opening an older index directly retains its original data/provenance.
See VALIDATION.md for exact scope. Release v0.1.3 packages this correction;
reopen the original replay to build a corrected index. Existing indices retain
their original data and provenance.

For v0.1.3, Release build and all three CTest targets passed. The fresh install
stage under ignored `build/release-v0.1.3-install/` passed its bundled research
packet checks, offline help rendering, a fresh small decode and native checks
on the corrected large index. The large replay was not rebuilt for packaging.
Release evidence stays under ignored `validation/release-v0.1.3/`; use the
packager with this stage, retaining exact committed-source certification and
the companion SHA256SUMS.txt. User-local AGENTS.md edits are excluded.
The v0.1.3 ZIP passed all 171 payload hashes and all 148 committed-source byte
comparisons; its extracted parser and EXE passed the same package checks.
