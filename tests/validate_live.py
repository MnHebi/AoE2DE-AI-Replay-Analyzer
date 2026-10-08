"""Optional integration certification against local replay evidence; no bundled replays."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import time


def cached_events(path):
    """Stream the existing JSON events array without loading its 200 MB report."""
    decoder=json.JSONDecoder()
    with Path(path).open(encoding="utf-8") as f:
        buffer=f.read(4096)
        start=buffer.index('"events"')
        start=buffer.index('[',start)+1
        buffer=buffer[start:]
        while True:
            buffer=buffer.lstrip(" \r\n\t,")
            if buffer.startswith("]"):
                return
            try:
                value,end=decoder.raw_decode(buffer)
            except json.JSONDecodeError:
                chunk=f.read(65536)
                if not chunk:
                    raise ValueError("Incomplete cached events array")
                buffer+=chunk
                continue
            yield value
            buffer=buffer[end:]


class MemoryCounters(ctypes.Structure):
    _fields_=[("cb",wintypes.DWORD),("PageFaultCount",wintypes.DWORD),
        ("PeakWorkingSetSize",ctypes.c_size_t),("WorkingSetSize",ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage",ctypes.c_size_t),("QuotaPagedPoolUsage",ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage",ctypes.c_size_t),("QuotaNonPagedPoolUsage",ctypes.c_size_t),
        ("PagefileUsage",ctypes.c_size_t),("PeakPagefileUsage",ctypes.c_size_t)]


def process_peak(process):
    if sys.platform!="win32":return None
    counters=MemoryCounters();counters.cb=ctypes.sizeof(counters)
    kernel=ctypes.WinDLL("kernel32",use_last_error=True)
    kernel.K32GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(MemoryCounters),wintypes.DWORD]
    if kernel.K32GetProcessMemoryInfo(wintypes.HANDLE(int(process._handle)),ctypes.byref(counters),counters.cb):
        return counters.PeakWorkingSetSize
    return None


def main():
    p=argparse.ArgumentParser();p.add_argument("replay");p.add_argument("--legacy-events");p.add_argument("--output",required=True)
    a=p.parse_args();root=Path(__file__).parents[1];output=Path(a.output).resolve();output.mkdir(parents=True,exist_ok=True)
    adapter=root/"backend"/"adapter.py";cache=output/"cache"
    base=[sys.executable,str(adapter),"build",a.replay,"--cache-dir",str(cache)]
    # Cancellation is discriminated from a normal failed decode, before any complete file is published.
    cancel_cache=output/"cancel-cache"
    cancelled=subprocess.Popen(base[:-1]+[str(cancel_cache)],stdout=subprocess.PIPE,text=True)
    for line in cancelled.stdout:
        row=json.loads(line)
        if row.get("phase")=="decoding":
            cancelled.kill();break
    cancelled.wait()
    assert not list(cancel_cache.glob("*.sqlite")), "Cancelled build published a complete cache"
    assert list(cancel_cache.glob("*.partial")), "Cancellation did not exercise an active stream"
    start=time.perf_counter();process=subprocess.Popen(base,stdout=subprocess.PIPE,text=True);ready=None;peak=0
    for line in process.stdout:
        row=json.loads(line);peak=max(peak,process_peak(process) or 0)
        if row.get("kind")=="ready":ready=row
    assert process.wait()==0 and ready, "Build failed"
    elapsed=time.perf_counter()-start
    second=subprocess.run(base,text=True,capture_output=True,check=True)
    assert json.loads(second.stdout.splitlines()[-1])["cache_hit"], "Unchanged replay/parser missed cache"
    database=Path(ready["database"]);db=sqlite3.connect(database)
    meta={k:json.loads(v) for k,v in db.execute("SELECT * FROM metadata")}
    with Path(a.replay).open("rb") as f:identity=hashlib.file_digest(f,"sha256").hexdigest()
    assert meta["provenance"]["replay_sha256"]==identity
    db.execute("CREATE UNIQUE INDEX IF NOT EXISTS events_sequence ON events(sequence)")
    matched=0
    if a.legacy_events:
        for event in cached_events(a.legacy_events):
            stored=db.execute("SELECT raw_json FROM events WHERE sequence=?",(event["sequence"],)).fetchone()
            assert stored, f"Missing legacy sequence {event['sequence']}"
            decoded=json.loads(stored[0])
            assert decoded==event, f"Divergent legacy sequence {event['sequence']}"
            matched+=1
    event_count=db.execute("SELECT count(*) FROM events").fetchone()[0]
    sample=db.execute("SELECT actor_id FROM event_actors LIMIT 1").fetchone()[0]
    query_start=time.perf_counter()
    query=subprocess.run([sys.executable,str(adapter),"query",str(database),"--request",json.dumps({"filters":{"actor_id":sample},"limit":250})],text=True,capture_output=True,check=True)
    query_s=time.perf_counter()-query_start
    result=json.loads(query.stdout);assert len(result["rows"])<=250
    summary={"database":str(database),"replay_sha256":identity,"pipeline_sha256":meta["provenance"]["pipeline_sha256"],
        "coverage":meta["coverage"],"event_count":event_count,"matched_legacy_events":matched,"build_seconds":round(elapsed,3),
        "peak_working_set_bytes":peak or None,"actor_query_seconds":round(query_s,3),"actor_query_total":result["total"],
        "database_bytes":database.stat().st_size,"cancelled_build_published_cache":False,"second_build_cache_hit":True}
    (output/"result.json").write_text(json.dumps(summary,indent=2))
    print(json.dumps(summary,indent=2))


if __name__=="__main__":main()
