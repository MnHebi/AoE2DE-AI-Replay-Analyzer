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
