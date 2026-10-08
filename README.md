# AoE2DE AI Replay Analyzer

A standalone C++20 / Qt 6 Widgets application for inspecting AoE2 DE replays
while developing and comparing AIs. Python retains replay decoding;
the C++ application owns the interface. It defaults to generic AoE2 operation,
without an AI installation, game-data payload or civilization/unit assumptions.

## Run on Windows

Download `AoE2ReplayAnalysis-Windows-x64.zip` from the
[latest GitHub release](https://github.com/MnHebi/AoE2DE-AI-Replay-Analyzer/releases/latest)
and extract it completely before opening `replay-analysis.exe`. The ZIP includes
Qt and compiler runtimes, the decoder, rebuildable sources and license notices.
Install Python 3.12+ separately; Python is not included. Release assets also
include `SHA256SUMS.txt` for checking the download.

Run `launch.ps1`, or open `dist/replay-analysis.exe` in the development folder.
In the release ZIP, the executable and launcher are together at the archive root.
Choose **File → Backend settings** and select a Python 3.12+ executable if one
was not found. The supplied decoder and helper directories are the defaults.
Choose **File → Open Replay** for a Definitive Edition `.aoe2record` file.
Recent paths and explicit user labels are stored in Qt settings. Derived indices
are stored in the per-user application cache, never in the AI source checkout.

The supplied `launch.ps1` uses the configured `AOE2_PYTHON` executable, a bundled
Codex Python 3 runtime when available, or an explicitly supplied `-Python` path.
The EXE also discovers Python directly: it validates the configured executable,
PATH candidates, the Windows Python launcher and common installation locations,
including an existing Codex runtime when available. Probes run asynchronously
and require working Python 3.12+ with SQLite. Windows Store shortcuts and older
interpreters are skipped. If discovery fails, select an installed interpreter
in Backend settings. Python 2 is not supported. No Python GUI libraries are used.

## Use

- Check players in the sidebar. Slot, selected color, team, civilization and
  human/computer designation come from the header, with unknowns retained.
- Filter events by action, category, evidence status, actors, targets, type ID,
  literal text and time range. Requests are named as requests. Time is in replay
  game milliseconds; the range controls use seconds.
- The activity chart uses the current player and event filters, while retaining
  the full time span as navigation context. Click a bin to open a time window
  around it in the Event explorer, on a page containing the nearest recorded
  event. The selected window is highlighted on the chart. Adjust the window
  length or enter exact range bounds.
- Tables request 250 rows at a time. Previous/Next change the page. Select a row
  to inspect its full decoded fields, packet bytes where captured, and source
  replay offset. Switching tabs preserves each page. Changed filters refresh
  the visible view; other views update when opened. Copy handles all selected
  rows in the current page. Invalid numeric IDs are outlined in red and pause
  queries and exports until corrected or cleared.
- Episodes group consecutive identical per-actor commands with at most 10 s
  between them. Their grouping is inferred and execution outcome unresolved.
  Double-click episodes or diagnostics to see exact supporting events.
- Generic diagnostics include at least 10 identical commands within 10 s,
  command gaps of at least 60 s, and unresolved packets. Rules and uncertainty
  are shown in each finding. These are inspection aids, not defect verdicts.
- Player analysis and Compare show decoded packet counts. Production, building
  and research counts do not establish completion. Simulation metrics stay
  unavailable. Comparisons honor the active event filters.
- **Open comparison replay** adds a reference replay to Compare. Its unfiltered
  counts, duration and settings are identified separately. The context row
  reports available map indicators, versions, game mode, settings fields and
  duration as matching, different or unavailable. Matching map IDs do not prove
  identical maps. Unknown ownership stays separate from player 0, and labels
  belong to the replay hash rather than its display filename. No aggregate
  score or claim of better AI is computed.
- Load optional JSON profile / game-data names under Sources. The format is
  illustrated in `profiles/example.json`; numeric string keys map to labels.
  Unit, building, technology and civilization namespaces remain separate.
  Profiles change display labels and export provenance, never raw packets.
- Assign player labels such as Baseline AI or Candidate AI, and optional manual
  identity. These are always marked user-provided.
- JSON and CSV exports include **all** filtered rows, not just the displayed
  page, plus parser/replay/profile/selection provenance. Summary export is text.
  Export runs outside the GUI thread. Loading can be cancelled; the previous
  loaded replay remains accessible. Incomplete indices are never reused.

## Build

Requires CMake 3.21+, a C++20 compiler, Qt 6.5+ Widgets, and Python 3.12+ for
analysis. Qt Charts is unnecessary: a small QPainter histogram provides the
needed timeline without an additional module. SQLite is in Python's standard
library and supports bounded-memory indexing and asynchronous paged querying.
With `BUILD_TESTING` enabled, the build also needs Qt Test and a Python 3.12+
interpreter discoverable by CMake (or supplied through `Python3_EXECUTABLE`).
Development executables can set `AOE2_BACKEND_ROOT` to the source directory;
installed executables use their adjacent backend without a compiled source path.

Cache location is configurable in Backend settings or with `--cache-dir PATH`.
The original cache-build filename/path and the currently opened replay path are
both retained in provenance when the same replay bytes are opened from another
location.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
cmake --install build --config Release --prefix dist
$env:AOE2_PYTHON = 'C:/Path/To/Python312/python.exe'
./dist/replay-analysis.exe
```

Linux uses the same sources and backend: install Qt 6 Widgets development files,
then `cmake -S . -B build`, `cmake --build build`, and set `AOE2_PYTHON` to Python
3.12+. Linux has not been runtime verified in this Windows workspace.

```powershell
& $env:AOE2_PYTHON -m unittest discover -s tests -v
ctest --test-dir build -C Release --output-on-failure
./dist/replay-analysis.exe --smoke-test --database C:/Path/To/index.sqlite --screenshot C:/Path/To/window.png
```

## Decoder provenance

The bundled `backend/vendor_helpers/analyze_replay.py` provides selected-color validation
and header extraction. `backend/vendor_helpers/audit_task_ownership.py` corrects DE WORK,
AI_ORDER, DE_RETREAT, SPECIAL and UNGARRISON packet IDs with length-verified
layouts. These helpers originated in Rome-at-War-AI; the decoder is the Kjir
mgz fork recorded in the supplied package metadata. Both are included locally,
so analysis and builds do not require that project's checkout.

This application bundles unchanged copies of those two helper modules and the
parser packages. The adapter calls the analyzer's existing header-only
entry point and the audited packet correction functions. It streams the same
`mgz.fast.operation` decoder directly into SQLite instead of accumulating the
existing JSON report's overlapping event lists. The existing CLIs and their
consumers are unchanged. Parser/helper directories can be configured for another
compatible installed backend; their contents are fingerprinted in each cache key.

Existing episode/benchmark tools depend on custom diagnostic IDs, source state,
AI-specific thresholds or external traces. They cannot substantiate generic
simulation outcomes from a replay alone. They are intentionally not applied as
generic classifiers. A future specialized adapter can add evidence-linked
results under a separately versioned contract. AI-specific code in the bundled
legacy helper is never invoked; only packet corrections and header functions run.

Support is restricted to DE `.aoe2record` headers and body operations the
configured decoder understands. Unsupported headers fail clearly. A partial
body stops at its first undecodable operation and shows the offset and coverage
warning. No byte resynchronization or unsupported-format support is claimed.

See `CONTRACT.md` for the backend interface and `VALIDATION.md` for executed
checks and remaining practical limits. Original AI source, PER files, game data
and deployment are untouched by this project.

## License

This project uses the repository's GNU GPLv3 license in `LICENSE`. Reused helper
modules and bundled dependencies retain their original license notices; see
`THIRD_PARTY.md` and `licenses/`.
