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
import io
import struct
import contextlib
from types import SimpleNamespace

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


class ResearchPacketTests(unittest.TestCase):
    packet = bytes.fromhex("01 0d 00 12 1e 00 00 01 00 16 00 ff ff ff ff 00")

    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).parents[1]
        sys.path.insert(0, str(cls.root / "backend/vendor"))
        sys.path.insert(0, str(cls.root / "backend/vendor_helpers"))
        from mgz import fast
        from mgz.fast import actions
        cls.fast, cls.actions = fast, actions

    @staticmethod
    def framed(inner, player=7):
        return struct.pack("<bh", player, len(inner)) + inner

    def action_record(self, packet, kind=101, sequence=42):
        return struct.pack("<IB", len(packet)+1, kind) + packet + struct.pack("<I", sequence)

    def test_exact_header_only_packet_is_observed_research(self):
        parsed = self.fast.parse_action(self.fast.Action.RESEARCH, self.packet)
        self.assertEqual(parsed, {"player_id":1,"object_ids":[7698],"technology_id":22})
        kind, parsed = self.fast.action(io.BytesIO(self.action_record(self.packet)))
        self.assertIs(kind, self.fast.Action.RESEARCH)
        self.assertEqual(parsed["raw_hex"], self.packet.hex())
        with sqlite3.connect(":memory:") as db:
            db.row_factory = sqlite3.Row
            db.executescript(adapter.DDL)
            adapter.append_event(db, dict(parsed,action=kind.name,milliseconds=123456,offset=789))
            event = db.execute("SELECT * FROM events").fetchone()
            self.assertEqual((event["player_id"],event["type_id"],event["actors"],event["status"]),
                             (1,22,"[7698]","observed"))
            self.assertEqual((event["time_ms"],event["sequence"],event["offset"]),(123456,42,789))
            self.assertEqual(event["category"],"research requests")
            self.assertEqual(json.loads(event["raw_json"])["raw_hex"],self.packet.hex())

    def test_list_bearing_packets_retain_existing_decoded_fields(self):
        for ids in ((7698,), (7698,8123)):
            with self.subTest(selected=len(ids)):
                inner = struct.pack("<Ihh5x",7698,len(ids),22) + struct.pack(f"<{len(ids)}I",*ids)
                packet = self.framed(inner)
                self.assertEqual(self.actions.parse_action_71094(self.fast.Action.RESEARCH,7,inner),
                                 {"player_id":7,"object_ids":[7698],"technology_id":22})
                kind, parsed = self.fast.action(io.BytesIO(self.action_record(packet)))
                self.assertIs(kind,self.fast.Action.RESEARCH)
                self.assertEqual(parsed, {"player_id":7,"object_ids":[7698],"technology_id":22,
                                          "raw_hex":packet.hex(),"sequence":42})

    def test_unsupported_inner_layouts_preserve_error_evidence(self):
        header = self.packet[3:]
        variants = [header[:n] for n in (0,5,12)] + [header+b"\x00"*n for n in (1,2,3,5,8)]
        variants += [struct.pack("<Ihh5xI",7698,2,22,7698),struct.pack("<Ihh5x",7698,-1,22)]
        for inner in variants:
            with self.subTest(length=len(inner),raw=inner.hex()):
                with self.assertRaises(struct.error):
                    self.actions.parse_action_71094(self.fast.Action.RESEARCH,7,inner)
                packet = self.framed(inner)
                stream = io.BytesIO(self.action_record(packet))
                kind,parsed = self.fast.action(stream)
                self.assertIs(kind,self.fast.Action.ERROR)
                self.assertEqual(stream.tell(),len(stream.getvalue()))
                self.assertEqual(parsed["original_action_id"],101)
                self.assertEqual(parsed["original_payload_length"],len(packet))
                self.assertEqual(parsed["raw_hex"],packet.hex())
                self.assertEqual(parsed["sequence"],42)
                self.assertTrue(parsed["decode_error"])
                self.assertNotIn("object_ids",parsed)
                self.assertNotIn("technology_id",parsed)

    def test_legacy_research_and_unrelated_valid_action_remain_supported(self):
        legacy = struct.pack("<3xIhh",7698,7,22)
        self.assertEqual(self.fast.parse_action(self.fast.Action.RESEARCH,legacy),
                         {"player_id":7,"object_ids":[7698],"technology_id":22})
        kind,parsed = self.fast.action(io.BytesIO(self.action_record(self.framed(b"\x00",5),
                                                                  self.fast.Action.RESIGN.value,77)))
        self.assertIs(kind,self.fast.Action.RESIGN)
        self.assertEqual(parsed,{"player_id":5,"sequence":77})

    def test_existing_struct_error_path_keeps_original_packet(self):
        kind,parsed = self.fast.action(io.BytesIO(self.action_record(b"\x01",self.fast.Action.MOVE.value)))
        self.assertIs(kind,self.fast.Action.ERROR)
        self.assertEqual(parsed["original_action_id"],self.fast.Action.MOVE.value)
        self.assertEqual(parsed["original_payload_length"],1)
        self.assertEqual(parsed["raw_hex"],"01")
        self.assertTrue(parsed["decode_error"])

    def test_unresolved_packet_does_not_stop_indexing_or_lose_evidence(self):
        malformed = self.framed(self.packet[3:]+b"\x00")
        operations = (struct.pack("<I",1)+self.action_record(malformed) +
                      struct.pack("<II",2,1500) +
                      struct.pack("<I",1)+self.action_record(self.packet,sequence=43))
        # A minimal body fixture isolates the packet-to-index path. Header metadata
        # and the unrelated log preamble are supplied independently of game data.
        with tempfile.TemporaryDirectory(dir=self.root) as temp:
            replay = Path(temp)/"fixture.aoe2record"
            replay.write_bytes(struct.pack("<II",8,0)+operations)
            args = SimpleNamespace(replay=str(replay),parser_root=str(self.root/"backend/vendor"),
                                   tools_root=str(self.root/"backend/vendor_helpers"),cache_dir=str(Path(temp)/"cache"))
            output = io.StringIO()
            head = {"players":[],"replay_settings":{}}
            with patch.object(adapter,"header_metadata",return_value=head), patch.object(self.fast,"meta"), contextlib.redirect_stdout(output):
                adapter.build(args)
            ready = json.loads(output.getvalue().splitlines()[-1])
            self.assertFalse(ready["cache_hit"])
            with contextlib.closing(adapter.open_db(ready["database"])) as db:
                rows = adapter.query(db,{})["rows"]
                self.assertEqual([(r["action"],r["status"]) for r in rows],[("ERROR","unresolved"),("RESEARCH","observed")])
                self.assertEqual([(r["sequence"],r["time_ms"]) for r in rows],[(1,0),(3,1500)])
                self.assertEqual(rows[0]["offset"],8)
                raw = json.loads(rows[0]["raw_json"])
                self.assertEqual(raw["raw_hex"],malformed.hex())
                self.assertEqual(raw["original_action_id"],101)
                self.assertEqual(raw["original_payload_length"],17)
                self.assertIn("RESEARCH",raw["decode_error"])
                self.assertIsNone(rows[0]["player_id"])
                self.assertIsNone(rows[0]["type_id"])
                self.assertEqual(rows[0]["actors"],[])
                findings = adapter.query(db,{"view":"diagnostics"})["rows"]
                self.assertEqual(len(findings),1)
                self.assertEqual(findings[0]["kind"],"unresolved packet")
                self.assertEqual((findings[0]["first_event"],findings[0]["last_event"]),(rows[0]["id"],rows[0]["id"]))
                self.assertEqual(adapter.metadata(db)["coverage"]["status"],"complete decoded command stream")
            # The real CLI must reuse this index; its synthetic header cannot be
            # decoded again. A child process also closes its SQLite handles.
            reuse = subprocess.run([sys.executable,str(Path(adapter.__file__)),"build",str(replay),
                                    "--cache-dir",args.cache_dir],capture_output=True,text=True,check=True,timeout=10)
            self.assertTrue(json.loads(reuse.stdout.splitlines()[-1])["cache_hit"])


if __name__ == "__main__":
    unittest.main()
