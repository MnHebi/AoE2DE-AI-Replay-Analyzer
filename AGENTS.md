# AoE2DE AI Replay Analyzer

This is an independent C++20 / Qt 6 Widgets application. Read `README.md`,
`CONTRACT.md` and `HANDOFF.md` before substantial changes. Verify the repository,
branch and dirty state before editing, and preserve existing work.

- The root `LICENSE` is GPLv3. Preserve original notices for the unchanged MIT
  helper modules and all bundled dependencies.
- Keep default analysis generic. Profiles and user labels are optional. Do not
  infer simulation outcomes, AI intent or successful execution from commands.
- Preserve raw IDs, packet fields, offsets, timestamps, unknowns and exact
  evidence membership. Distinguish observations from inferred groupings.
  Preserve bundled backend file bytes; their hashes identify the parser pipeline.
- Python owns decoding/indexing; Qt owns the interface. Preserve the versioned
  contract, asynchronous process work, bounded table pages and cancellation.
- No game installation, AI source checkout or private machine path is required.
  Do not commit replays, game data, caches, SDKs, build outputs or local evidence.
- Use Python 3.12+ explicitly. For backend changes run the focused tests in
  `tests/test_adapter.py`; for native changes build Release and run CTest. Check
  the installed executable when packaging or changing dependency paths.
- Record executed checks and limits in `VALIDATION.md` and update `HANDOFF.md`.
  A passing static check never proves gameplay behavior or a replay format that
  has not been tested.
