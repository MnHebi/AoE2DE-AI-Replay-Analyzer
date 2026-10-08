"""Contract checks use replay-independent fixtures with missing and conflicting data."""
import importlib.util
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch
import subprocess
import sys

spec = importlib.util.spec_from_file_location("adapter", Path(__file__).parents[1] / "backend" / "adapter.py")
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class ContractTests(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.db.row_factory = sqlite3.Row
        self.db.executescript(adapter.DDL)
        adapter.set_meta(self.db, "schema_version", adapter.SCHEMA)
        adapter.set_meta(self.db, "provenance", {"replay_sha256": "fixture", "parser": "fixture"})
        for i in range(12):
            # Player 7 and actor 400 intentionally do not imply color, civilization or AI.
            adapter.append_event(self.db, dict(action="ORDER", player_id=7, object_ids=[400], target_id=900,
                milliseconds=i*400, sequence=i+1, offset=100+i*20))
        adapter.append_event(self.db, dict(action="ORDER", player_id=2, object_ids=[400], target_id=900,
            milliseconds=5500, sequence=13, offset=360))
        adapter.append_event(self.db, dict(action="MOVE", object_ids=[800], target_id=0xffffffff,
            milliseconds=6500, sequence=14, offset=400))
        adapter.append_event(self.db, dict(action="WORK", player_id=7, decode_error="bad object array", raw_hex="deadbeef",
            milliseconds=7000, sequence=15, offset=450))
        adapter.append_event(self.db, dict(action="DE_QUEUE", player_id=2, object_ids=[1000], unit_id=9999, amount=3,
            milliseconds=80000, sequence=16, offset=500))
        adapter.reconstruct(self.db)

    def tearDown(self):
        self.db.close()

    def test_selection_unknowns_and_empty_selection(self):
        result = adapter.query(self.db, {"filters": {"players": [7]}})
        self.assertEqual(result["total"], 13)
        self.assertEqual(adapter.query(self.db, {"filters": {"players": []}})["total"], 0)
        result = adapter.query(self.db, {"filters": {"players": [], "include_unknown": True}})
        self.assertEqual(result["total"], 1)
        row = result["rows"][0]
        self.assertIsNone(row["player_id"])
        self.assertIsNone(row["target_id"])
        self.assertEqual(json.loads(row["raw_json"])["target_id"], 0xffffffff)

    def test_episode_exact_evidence_and_owner_separation(self):
        episodes = adapter.query(self.db, {"view": "episodes"})["rows"]
        self.assertEqual(len(episodes), 1)
        episode = episodes[0]
        self.assertEqual((episode["player_id"], episode["event_count"], episode["outcome"]), (7, 12, "unresolved"))
        evidence = adapter.query(self.db, {"filters": {"episode_id": episode["id"]}})
        self.assertEqual(evidence["total"], 12)
        self.assertTrue(all(e["player_id"]==7 for e in evidence["rows"]))
        findings = adapter.query(self.db, {"view": "diagnostics"})["rows"]
        self.assertEqual({r["kind"] for r in findings}, {"repeated identical commands", "recorded command gap", "unresolved packet"})
        self.assertTrue(all(r["status"] in {"inferred", "unresolved"} for r in findings))

    def test_actor_target_type_and_time_filters(self):
        self.assertEqual(adapter.query(self.db, {"filters": {"actor_id": 400, "target_id": 900, "from_ms": 1000, "to_ms": 2500}})["total"], 4)
        row = adapter.query(self.db, {"filters": {"type_id": 9999}})["rows"][0]
        self.assertEqual(row["action"], "DE_QUEUE")
        self.assertEqual(row["category"], "production requests")
        self.assertEqual(json.loads(row["raw_json"])["amount"], 3)
        self.assertEqual(adapter.query(self.db, {"filters": {"search": "%"}})["total"], 0)

    def test_bounded_pages_and_no_simulation_zeroes(self):
        result=adapter.query(self.db,{"limit":2,"offset":3})
        self.assertEqual((result["total"],len(result["rows"]),result["rows"][0]["id"]),(16,2,4))
        self.assertTrue(all(v is None for v in adapter.statistics(self.db,{})["simulation_metrics"].values()))
        self.assertEqual(adapter.query(self.db,{"limit":100000})["limit"],500)
        timeline=adapter.query(self.db,{"view":"timeline"})
        self.assertEqual(sum(r["count"] for r in timeline["bins"]),16)

    def test_time_anchor_uses_filtered_bounded_page(self):
        for i in range(1000):
            adapter.append_event(self.db, dict(action="MOVE", player_id=7, object_ids=[500],
                milliseconds=100000+i*1000, sequence=100+i, offset=1000+i))
        request = {"view": "events", "filters": {"players": [7], "from_ms": 100000},
                   "anchor_ms": 850000, "limit": 250}
        page = adapter.query(self.db, request)
        self.assertEqual((page["total"], page["offset"], len(page["rows"])), (1000, 625, 250))
        self.assertEqual(page["rows"][125]["time_ms"], 850000)
        request["anchor_ms"] = 2000000
        page = adapter.query(self.db, request)
        self.assertEqual((page["offset"], len(page["rows"])), (750, 250))
        request["filters"]["players"] = []
        page = adapter.query(self.db, request)
        self.assertEqual((page["offset"], page["total"], page["rows"]), (0, 0, []))

    def test_exports_all_filtered_rows_and_selection_provenance(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parents[1]) as temp:
            path=Path(temp)/"filtered.json"
            request={"view":"events","filters":{"players":[7]},"user_labels":{"7":"Candidate AI"},"profile":{"name":"Example"}}
            adapter.export(self.db,request,path,"json")
            data=json.loads(path.read_text())
            self.assertEqual(len(data["events"]),13)
            self.assertEqual(data["provenance"]["selection"],request)
            self.assertEqual(self.db.execute("SELECT count(*) FROM events").fetchone()[0],16)
            csv=Path(temp)/"events.csv"
            adapter.export(self.db,request,csv,"csv")
            self.assertIn("export_provenance",csv.read_text())

    def test_reject_unknown_view(self):
        with self.assertRaises(ValueError):
            adapter.query(self.db,{"view":"invented"})

    def test_protected_export_destination_does_not_retry_permission(self):
        with patch.object(Path,"open",side_effect=PermissionError("protected")) as opening:
            with self.assertRaises(PermissionError):
                adapter.temporary_file("protected","export.")
            self.assertEqual(opening.call_count,1)

    def test_damaged_replay_cannot_publish_cache(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parents[1]) as temp:
            source=Path(temp)/"broken.aoe2record"
            source.write_bytes(b"not a replay")
            process=subprocess.run([sys.executable,str(Path(adapter.__file__)),"build",str(source),"--cache-dir",str(Path(temp)/"cache")],capture_output=True,text=True,timeout=10)
            self.assertNotEqual(process.returncode,0)
            self.assertEqual(json.loads(process.stdout.splitlines()[-1])["kind"],"error")
            self.assertFalse(list((Path(temp)/"cache").glob("*.sqlite")))
            self.assertEqual(source.read_bytes(),b"not a replay")


if __name__ == "__main__":
    unittest.main()
