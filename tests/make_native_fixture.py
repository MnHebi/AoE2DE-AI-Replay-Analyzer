"""Generate a replay-independent index and log actual native backend requests."""
import importlib.util
import json
from pathlib import Path
import sqlite3
import sys

destination, source = map(Path, sys.argv[1:3])
spec = importlib.util.spec_from_file_location("fixture_adapter", source / "backend/adapter.py")
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)
with sqlite3.connect(destination / "fixture.sqlite") as db:
    db.executescript(adapter.DDL)
    for i in range(800):
        adapter.append_event(db, dict(action="ORDER", player_id=[0, 7, None][i % 3],
            object_ids=[400], target_id=900, milliseconds=100000+i*500,
            sequence=i+1, offset=100+i*20))
    adapter.reconstruct(db)
    for player in (0, 7):
        db.execute("INSERT INTO players VALUES (?,?)", (player, json.dumps({
            "number": player, "name": f"Fixture {player}", "designation": "unavailable"})))
    metadata = {
        "schema_version": adapter.SCHEMA, "complete": True,
        "replay": {"filename": "same.aoe2record", "duration_ms": 500000,
                   "settings": {"game_version": "VER 9.4", "rms_map_id": 42,
                                "rms_filename": None, "game_type_id": None}},
        "provenance": {"replay_sha256": "current-fixture", "pipeline_sha256": "fixture"},
        "coverage": {"events": 800}, "warnings": [], "limitations": [],
    }
    for key, value in metadata.items():
        adapter.set_meta(db, key, value)

backend = destination / "backend"
backend.mkdir()
(backend / "adapter.py").write_text(f'''
import importlib.util, json, sys
from pathlib import Path
if len(sys.argv)>1 and sys.argv[1]=="query":
    with Path({str(destination / "queries.jsonl")!r}).open("a",encoding="utf-8") as log:
        log.write(sys.argv[sys.argv.index("--request")+1]+"\\n")
spec=importlib.util.spec_from_file_location("real_adapter",{str(source / "backend/adapter.py")!r})
adapter=importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)
adapter.main()
''', encoding="utf-8")
