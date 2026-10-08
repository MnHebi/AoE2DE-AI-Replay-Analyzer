#!/usr/bin/env python3
"""Versioned, streaming bridge to the existing mgz decoder. No AI assumptions."""
from __future__ import annotations

import argparse
import contextlib
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import sqlite3
import struct
import sys
import time
import uuid

SCHEMA = 1
ROOT = Path(__file__).resolve().parent
CATEGORIES = {
    "movement": {"MOVE", "ADD_WAYPOINT", "GROUP_MULTI_WAYPOINTS", "DE_RETREAT"},
    "combat commands": {"PATROL", "DE_ATTACK_MOVE", "ATTACK_GROUND"},
    "work commands": {"WORK", "REPAIR", "BACK_TO_WORK"},
    "construction requests": {"BUILD", "WALL"},
    "production requests": {"MAKE", "QUEUE", "MULTIQUEUE", "DE_QUEUE", "CREATE"},
    "research requests": {"RESEARCH"},
    "economy commands": {"BUY", "SELL", "TRIBUTE", "DE_TRIBUTE"},
    "chat": {"CHAT"},
    "orders": {"ORDER", "AI_ORDER", "STOP", "SPECIAL", "GUARD", "FOLLOW", "UNGARRISON"},
}
LIMITATIONS = [
    "Issued commands do not establish successful execution, production, research, combat, or gathering.",
    "Missing commands do not establish idle units, deaths, boarding, or an AI defect.",
    "Object types, ownership changes, simulation state and match outcome are unavailable unless explicitly decoded.",
    "Generic episodes group consecutive commands per actor; their execution outcome is unresolved.",
    "AI script goals and decision conditions are unavailable. AI_ORDER is a packet kind, not proof of an AI player.",
]


def safe(value):
    if isinstance(value, bytes):
        return {"hex": value.hex()}
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, dict):
        return {str(k): safe(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [safe(v) for v in value]
    if hasattr(value, "name"):
        return value.name
    return value


def emit(value):
    print(json.dumps(safe(value), ensure_ascii=True, allow_nan=False), flush=True)


def digest_file(path):
    with Path(path).open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def temporary_file(directory, prefix):
    # tempfile.mkstemp retries PermissionError thousands of times on Windows.
    # Fail promptly on a protected destination; retry only a name collision.
    for _ in range(8):
        candidate = Path(directory) / (prefix + uuid.uuid4().hex + ".partial")
        try:
            with candidate.open("xb"):
                pass
            return str(candidate)
        except FileExistsError:
            continue
    raise FileExistsError("Could not create an unused temporary filename")


def fingerprint(parser_root, tools_root):
    h = hashlib.sha256()
    paths = [ROOT / "adapter.py"]
    for package in ("mgz", "construct", "aocref"):
        paths += sorted((parser_root / package).rglob("*.py"))
        paths += sorted((parser_root / package).rglob("*.json"))
    paths += [tools_root / "analyze_replay.py", tools_root / "audit_task_ownership.py"]
    for path in paths:
        h.update(path.name.encode())
        h.update(bytes.fromhex(digest_file(path)))
    return h.hexdigest()


def category(action):
    return next((k for k, v in CATEGORIES.items() if action in v), "other")


def integer(value):
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def header_metadata(replay, parser_root, tools_root):
    sys.path[:0] = [str(tools_root), str(parser_root)]
    import analyze_replay
    from mgz.fast import header
    old = sys.argv
    output = io.StringIO()
    try:
        sys.argv = ["analyze_replay.py", str(parser_root), str(replay), "--header-only"]
        with contextlib.redirect_stdout(output):
            analyze_replay.main()
    finally:
        sys.argv = old
    result = json.loads(output.getvalue())
    if result.get("header_parse_error"):
        raise ValueError("Unsupported or damaged DE header: " + result["header_parse_error"])
    with replay.open("rb") as f:
        block = header.decompress(f)
        version, game, save, log = header.parse_version(block, f)
        if version.name != "DE":
            raise ValueError("This adapter currently supports decoded Definitive Edition .aoe2record files only")
        de = header.parse_de(block, version, save)
    by_number = {p["number"]: p for p in de.get("players", [])}
    for p in result["players"]:
        source = by_number[p["number"]]
        p["name"] = analyze_replay.decode_de_string(source.get("name"))
        p["player_type_id"] = source.get("type")
        p["designation"] = {2: "human", 4: "computer", 5: "cyborg", 6: "spectator"}.get(source.get("type"), "unavailable")
        p["designation_basis"] = "observed replay header type"
    result["replay_settings"].update({
        k: safe(de.get(k)) for k in ("build", "game_type_id", "starting_age_id", "ending_age_id", "reveal_map_id", "victory_type_id") if de.get(k) is not None
    })
    result["parser_metadata_warnings"] = []
    if save >= 61.5:
        result["parser_metadata_warnings"].append(
            "Difficulty ID is the decoder's raw byte interpretation. Its mapping for save versions >=61.5 is explicitly uncertain in the parser; no named difficulty is asserted."
        )
    return result


DDL = """
CREATE TABLE metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE players(number INTEGER PRIMARY KEY, data TEXT NOT NULL);
CREATE TABLE events(id INTEGER PRIMARY KEY, time_ms INTEGER NOT NULL, sequence INTEGER NOT NULL,
 offset INTEGER, player_id INTEGER, action TEXT NOT NULL, category TEXT NOT NULL,
 status TEXT NOT NULL, target_id INTEGER, type_id INTEGER, actors TEXT NOT NULL, signature TEXT,
 raw_json TEXT NOT NULL);
CREATE TABLE event_actors(event_id INTEGER NOT NULL, actor_id INTEGER NOT NULL,
 PRIMARY KEY(event_id, actor_id));
CREATE TABLE episodes(id INTEGER PRIMARY KEY, player_id INTEGER, actor_id INTEGER,
 start_ms INTEGER, end_ms INTEGER, action TEXT, target_id INTEGER, event_count INTEGER,
 first_event INTEGER, last_event INTEGER, status TEXT, outcome TEXT, uncertainty TEXT);
CREATE TABLE episode_events(episode_id INTEGER, event_id INTEGER, PRIMARY KEY(episode_id,event_id));
CREATE TABLE diagnostics(id INTEGER PRIMARY KEY, player_id INTEGER, start_ms INTEGER, end_ms INTEGER,
 kind TEXT, status TEXT, first_event INTEGER, last_event INTEGER, episode_id INTEGER,
 description TEXT, uncertainty TEXT);
"""


def set_meta(db, key, value):
    db.execute("INSERT OR REPLACE INTO metadata VALUES (?,?)", (key, json.dumps(safe(value))))


def append_event(db, event):
    player = integer(event.get("player_id", event.get("player")))
    actors = sorted(set(v for v in event.get("object_ids", []) if integer(v) is not None and v >= 0))
    if event.get("decode_error"):
        actors = []
    action = event["action"]
    target = integer(event.get("target_id"))
    # -1 and 0xffffffff are absent-target sentinels, preserved in raw_json.
    target = target if target is not None and target >= 0 and target != 0xffffffff else None
    type_id = integer(event.get("unit_id", event.get("building_id", event.get("technology_id"))))
    status = "unresolved" if event.get("decode_error") or action == "ERROR" else "observed"
    signature = None
    if actors and status == "observed":
        signature = json.dumps({k: safe(event[k]) for k in sorted(event) if k not in {
            "object_ids", "sequence", "milliseconds", "offset", "raw_hex", "player_id", "player"
        }}, sort_keys=True)
    cursor = db.execute("INSERT INTO events(time_ms,sequence,offset,player_id,action,category,status,target_id,type_id,actors,signature,raw_json) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)",
        (event["milliseconds"], event["sequence"], event.get("offset"), player, action, category(action), status,
         target, type_id, json.dumps(actors), signature, json.dumps(safe(event))))
    db.executemany("INSERT INTO event_actors VALUES (?,?)", ((cursor.lastrowid, a) for a in actors))


def reconstruct(db):
    """SQL windows keep actor history and evidence on disk, including unknown owners."""
    db.executescript("""
    CREATE INDEX events_time ON events(time_ms,id);
    CREATE UNIQUE INDEX events_sequence ON events(sequence);
    CREATE INDEX events_player_time ON events(player_id,time_ms,id);
    CREATE INDEX events_action_time ON events(action,time_ms,id);
    CREATE INDEX events_category_time ON events(category,time_ms,id);
    CREATE INDEX events_target ON events(target_id,time_ms);
    CREATE INDEX events_type ON events(type_id,time_ms);
    CREATE INDEX actors_id ON event_actors(actor_id,event_id);
    CREATE TEMP TABLE runs AS
      WITH previous AS (
       SELECT e.*, a.actor_id,
        lag(e.signature) OVER w AS previous_sig, lag(e.time_ms) OVER w AS previous_ms
       FROM event_actors a JOIN events e ON e.id=a.event_id
       WINDOW w AS (PARTITION BY e.player_id,a.actor_id ORDER BY e.id)
      ), boundaries AS (
       SELECT *, CASE WHEN signature IS NOT NULL AND signature=previous_sig
         AND time_ms-previous_ms<=10000 THEN 0 ELSE 1 END AS boundary FROM previous
      ) SELECT *, sum(boundary) OVER(PARTITION BY player_id,actor_id ORDER BY id) AS run_id FROM boundaries;
    CREATE INDEX runs_group ON runs(player_id,actor_id,run_id);
    CREATE INDEX runs_actor_event ON runs(actor_id,player_id,id);
    INSERT INTO episodes(player_id,actor_id,start_ms,end_ms,action,target_id,event_count,first_event,last_event,status,outcome,uncertainty)
      SELECT player_id,actor_id,min(time_ms),max(time_ms),action,target_id,count(*),min(id),max(id),
       'inferred','unresolved','Command grouping does not establish execution success or an AI defect.'
      FROM runs WHERE signature IS NOT NULL GROUP BY player_id,actor_id,run_id HAVING count(*)>=2;
    INSERT INTO episode_events
      SELECT p.id,r.id FROM episodes p JOIN runs r ON r.actor_id=p.actor_id
       AND r.player_id IS p.player_id AND r.id BETWEEN p.first_event AND p.last_event;
    INSERT INTO diagnostics(player_id,start_ms,end_ms,kind,status,first_event,last_event,episode_id,description,uncertainty)
      SELECT player_id,start_ms,end_ms,'repeated identical commands','inferred',first_event,last_event,id,
       event_count || ' consecutive commands for actor ' || actor_id || ' in ' || ((end_ms-start_ms)/1000.0) || ' seconds. Rule: at least 10 commands in at most 10 seconds.',
       'May be intentional. Command success and internal cause remain unresolved.'
      FROM episodes WHERE event_count>=10 AND end_ms-start_ms<=10000;
    INSERT INTO diagnostics(player_id,start_ms,end_ms,kind,status,first_event,last_event,description,uncertainty)
      WITH gaps AS (
       SELECT *,lag(time_ms) OVER w AS before_ms,lag(id) OVER w AS before_id FROM events
       WHERE player_id IS NOT NULL AND category!='chat'
       WINDOW w AS(PARTITION BY player_id ORDER BY id)
      ) SELECT player_id,before_ms,time_ms,'recorded command gap','inferred',before_id,id,
       ((time_ms-before_ms)/1000.0) || ' seconds between recorded commands. Rule: at least 60 seconds.',
       'Does not establish unit inactivity; autonomous actions may continue.'
      FROM gaps WHERE time_ms-before_ms>=60000;
    INSERT INTO diagnostics(player_id,start_ms,end_ms,kind,status,first_event,last_event,description,uncertainty)
      SELECT player_id,time_ms,time_ms,'unresolved packet','unresolved',id,id,
       'Packet could not be fully decoded. Inspect original bytes and parser warning.',
       'Actor, target and interpretation may be unavailable.' FROM events WHERE status='unresolved';
    DROP TABLE runs;
    """)


def build(args):
    replay, parser_root, tools_root = map(lambda p: Path(p).resolve(), (args.replay, args.parser_root, args.tools_root))
    if replay.suffix.lower() != ".aoe2record":
        raise ValueError("Only .aoe2record is enabled; other formats have not been verified")
    emit({"kind": "progress", "phase": "identity", "percent": 0})
    replay_hash = digest_file(replay)
    pipeline_hash = fingerprint(parser_root, tools_root)
    cache_dir = Path(args.cache_dir).resolve()
    cache_dir.mkdir(parents=True, exist_ok=True)
    destination = cache_dir / f"{replay_hash}-{pipeline_hash[:20]}-v{SCHEMA}.sqlite"
    if destination.exists():
        try:
            with open_db(destination) as existing:
                meta = metadata(existing)
                if meta.get("complete") and meta["provenance"]["pipeline_sha256"] == pipeline_hash:
                    emit({"kind": "ready", "database": str(destination), "cache_hit": True})
                    return
        except (sqlite3.Error, ValueError, KeyError):
            pass
    head = header_metadata(replay, parser_root, tools_root)
    from mgz import fast
    from mgz.fast import actions
    from audit_task_ownership import decode_packet, COMMANDS
    from analyze_replay import decode_chat
    original = actions.parse_action_71094
    old_fast = fast.parse_action_71094

    def capture(kind, player, raw):
        parsed = original(kind, player, raw)
        if kind.name in COMMANDS:
            try:
                parsed = decode_packet(kind.name, raw, parsed)
            except (ValueError, struct.error) as error:
                parsed = {"player_id": player, "decode_error": str(error)}
            parsed["raw_hex"] = raw.hex()
        return parsed

    actions.parse_action_71094 = fast.parse_action_71094 = capture
    # A killed process leaves only a .partial file, never a cache eligible for reuse.
    temporary = temporary_file(cache_dir, destination.stem + "-")
    db = sqlite3.connect(temporary)
    db.execute("PRAGMA cache_size=-16384")
    db.execute("PRAGMA temp_store=FILE")
    db.executescript(DDL)
    warnings = list(head.get("parser_metadata_warnings", []))
    if head.get("visible_player_metadata_error"):
        warnings.append(head["visible_player_metadata_error"])
    sequence = game_time = count = 0
    size = replay.stat().st_size
    last_progress = time.monotonic()
    coverage = "complete decoded command stream"
    try:
        with replay.open("rb") as f:
            length = struct.unpack("<I", f.read(4))[0]
            if length < 8 or length >= size:
                raise ValueError("Invalid replay header length")
            f.seek(length)
            fast.meta(f)
            while f.tell() < size:
                offset = f.tell()
                sequence += 1
                try:
                    op, payload = fast.operation(f)
                except (EOFError, RuntimeError, ValueError, struct.error) as error:
                    warnings.append(f"Stopped at offset {offset}, time {game_time} ms: {type(error).__name__}: {error}")
                    coverage = "partial command stream; stopped at first undecodable operation"
                    break
                if op.name == "SYNC":
                    increment, _, sync = payload
                    game_time = max(game_time + increment, sync.get("current_time", 0))
                elif op.name == "ACTION":
                    kind, record = payload
                    append_event(db, dict(record, sequence=sequence, milliseconds=game_time, offset=offset, action=kind.name))
                    count += 1
                elif op.name == "CHAT":
                    append_event(db, dict(decode_chat(payload), sequence=sequence, milliseconds=game_time, offset=offset, action="CHAT"))
                    count += 1
                if time.monotonic() - last_progress > 0.3:
                    db.commit()
                    emit({"kind": "progress", "phase": "decoding", "percent": int(f.tell() * 85 / size), "events": count})
                    last_progress = time.monotonic()
        db.commit()
        emit({"kind": "progress", "phase": "indexing and reconstructing", "percent": 88, "events": count})
        reconstruct(db)
        for p in head["players"]:
            db.execute("INSERT INTO players VALUES (?,?)", (p["number"], json.dumps(p)))
        set_meta(db, "schema_version", SCHEMA)
        set_meta(db, "replay", {"filename": replay.name, "duration_ms": game_time, "duration_basis": "last decoded synchronization", "outcome": None, "settings": head["replay_settings"]})
        set_meta(db, "provenance", {"replay_sha256": replay_hash, "source_path": str(replay), "pipeline_sha256": pipeline_hash,
            "parser": "mgz (configured source fingerprint)", "parser_root": str(parser_root), "tools_root": str(tools_root), "adapter_schema": SCHEMA})
        set_meta(db, "warnings", warnings)
        set_meta(db, "limitations", LIMITATIONS)
        set_meta(db, "coverage", {"status": coverage, "events": count, "operations": sequence,
            "unresolved_packets": db.execute("SELECT count(*) FROM events WHERE status='unresolved'").fetchone()[0]})
        set_meta(db, "complete", True)
        db.commit()
        db.close()
        os.replace(temporary, destination)
        emit({"kind": "ready", "database": str(destination), "cache_hit": False})
    finally:
        actions.parse_action_71094, fast.parse_action_71094 = original, old_fast
        db.close()
        if Path(temporary).exists():
            Path(temporary).unlink()


def open_db(path):
    db = sqlite3.connect(Path(path).resolve().as_uri() + "?mode=ro", uri=True)
    db.row_factory = sqlite3.Row
    version = db.execute("SELECT value FROM metadata WHERE key='schema_version'").fetchone()
    if not version or json.loads(version[0]) != SCHEMA:
        db.close()
        raise ValueError("Unsupported database schema")
    return db


def metadata(db):
    return {r[0]: json.loads(r[1]) for r in db.execute("SELECT * FROM metadata")}


def where(filters, view):
    clauses, params = [], []
    if "players" in filters:
        selected = [int(p) for p in filters["players"]]
        player_clause = "player_id IN (" + ",".join("?" for _ in selected) + ")" if selected else "0"
        if filters.get("include_unknown", False):
            player_clause = "(" + player_clause + " OR player_id IS NULL)"
        clauses.append(player_clause)
        params += selected
    if view == "events" and filters.get("ids"):
        selected_ids = [int(v) for v in filters["ids"]]
        clauses.append("id IN (" + ",".join("?" for _ in selected_ids) + ")")
        params += selected_ids
    start = "time_ms" if view == "events" else "start_ms"
    end = "time_ms" if view == "events" else "end_ms"
    for field, op, key in ((end, ">=", "from_ms"), (start, "<=", "to_ms")):
        if filters.get(key) is not None:
            clauses.append(f"{field}{op}?")
            params.append(int(filters[key]))
    if view == "events":
        for key in ("action", "category", "status", "target_id", "type_id", "id"):
            if filters.get(key) not in (None, ""):
                clauses.append(key + "=?")
                params.append(filters[key])
        if filters.get("actor_id") is not None:
            clauses.append("id IN (SELECT event_id FROM event_actors WHERE actor_id=?)")
            params.append(int(filters["actor_id"]))
        if filters.get("episode_id") is not None:
            clauses.append("id IN (SELECT event_id FROM episode_events WHERE episode_id=?)")
            params.append(int(filters["episode_id"]))
        if filters.get("search"):
            clauses.append("raw_json LIKE ? ESCAPE '\\'")
            s = str(filters["search"]).replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_")
            params.append("%" + s + "%")
    elif filters.get("status"):
        clauses.append("status=?")
        params.append(filters["status"])
    return (" WHERE " + " AND ".join(clauses) if clauses else ""), params


def statistics(db, filters):
    w, params = where(filters, "events")
    rows = [dict(r) for r in db.execute("SELECT player_id,category,count(*) AS count FROM events" + w + " GROUP BY player_id,category", params)]
    return {"counts": rows, "basis": "observed decoded packet counts; requests are not completed actions",
            "simulation_metrics": {k: None for k in ("units_produced", "resources_gathered", "units_idle", "kills", "winner")}}


def query(db, request):
    view = request.get("view", "events")
    filters = request.get("filters", {})
    if view == "overview":
        result = metadata(db)
        result["players"] = [json.loads(r[0]) for r in db.execute("SELECT data FROM players ORDER BY number")]
        result["actions"] = [r[0] for r in db.execute("SELECT DISTINCT action FROM events ORDER BY action")]
        result["categories"] = [r[0] for r in db.execute("SELECT DISTINCT category FROM events ORDER BY category")]
        result["statistics"] = statistics(db, {})
        return result
    if view == "statistics":
        return statistics(db, filters)
    if view == "timeline":
        w, params = where(filters, "events")
        lo, hi = db.execute("SELECT min(time_ms),max(time_ms) FROM events" + w, params).fetchone()
        width = max(1000, ((hi or 0) - (lo or 0)) // 160 + 1)
        rows = [dict(r) for r in db.execute("SELECT ((time_ms-?)/?) AS bin,count(*) AS count FROM events" + w + " GROUP BY bin ORDER BY bin", [lo or 0, width] + params)]
        return {"from_ms": lo or 0, "bin_ms": width, "bins": rows}
    if view not in {"events", "episodes", "diagnostics"}:
        raise ValueError("Unknown query view")
    w, params = where(filters, view)
    total = db.execute(f"SELECT count(*) FROM {view}" + w, params).fetchone()[0]
    limit = max(1, min(int(request.get("limit", 250)), 500))
    offset = max(0, int(request.get("offset", 0)))
    order = "time_ms,id" if view == "events" else "start_ms,id"
    rows = [dict(r) for r in db.execute(f"SELECT * FROM {view}" + w + f" ORDER BY {order} LIMIT ? OFFSET ?", params + [limit, offset])]
    for row in rows:
        row.pop("signature", None)
        if "actors" in row:
            row["actors"] = json.loads(row["actors"])
    return {"view": view, "total": total, "offset": offset, "limit": limit, "rows": rows}


def export(db, request, output, fmt):
    view = request.get("view", "events")
    if view not in {"events", "episodes", "diagnostics"}:
        raise ValueError("Unsupported export table")
    w, params = where(request.get("filters", {}), view)
    provenance = metadata(db)
    provenance["selection"] = request
    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temp = temporary_file(destination.parent, destination.name + ".")
    try:
        with open(temp, "w", encoding="utf-8", newline="") as f:
            if fmt == "summary":
                f.write("AoE2 Replay Analysis\n\n")
                f.write(json.dumps({"provenance": provenance, "statistics": statistics(db, request.get("filters", {}))}, indent=2, ensure_ascii=False))
            else:
                cursor = db.execute(f"SELECT * FROM {view}" + w + " ORDER BY id", params)
                if fmt == "csv":
                    writer = csv.writer(f)
                    writer.writerow([d[0] for d in cursor.description] + ["export_provenance"])
                    encoded = json.dumps(provenance, ensure_ascii=False)
                    for row in cursor:
                        writer.writerow(list(row) + [encoded])
                else:
                    f.write('{"provenance":' + json.dumps(provenance, ensure_ascii=False) + ',"' + view + '":[')
                    for i, row in enumerate(cursor):
                        if i:
                            f.write(",")
                        f.write(json.dumps(dict(row), ensure_ascii=False))
                    f.write("]}")
        os.replace(temp, destination)
    finally:
        if Path(temp).exists():
            Path(temp).unlink()
    return {"exported": str(destination)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    b = sub.add_parser("build")
    b.add_argument("replay")
    b.add_argument("--parser-root", default=str(ROOT / "vendor"))
    b.add_argument("--tools-root", default=str(ROOT / "vendor_helpers"))
    b.add_argument("--cache-dir", required=True)
    q = sub.add_parser("query")
    q.add_argument("database")
    q.add_argument("--request", default="{}")
    e = sub.add_parser("export")
    e.add_argument("database")
    e.add_argument("output")
    e.add_argument("--format", choices=("json", "csv", "summary"), default="json")
    e.add_argument("--request", default="{}")
    args = p.parse_args()
    try:
        if args.command == "build":
            build(args)
        else:
            with open_db(args.database) as db:
                request = json.loads(args.request)
                emit(query(db, request) if args.command == "query" else export(db, request, args.output, args.format))
    except Exception as error:
        emit({"kind": "error", "message": f"{type(error).__name__}: {error}"})
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
