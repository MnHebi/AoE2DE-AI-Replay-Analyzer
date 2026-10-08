"""Package the installed application plus rebuildable C++ sources; exclude evidence."""
import hashlib
import io
import argparse
from datetime import date
import json
from pathlib import Path
import sys
import subprocess
import zipfile

root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/"backend"))
from adapter import fingerprint
arguments=argparse.ArgumentParser(description=__doc__)
arguments.add_argument("--dist",type=Path,default=root/"dist")
arguments.add_argument("--output",type=Path,default=root/"AoE2ReplayAnalysis-Windows-x64.zip")
options=arguments.parse_args()
dist=options.dist.resolve()
destination=options.output.resolve()
selected={p.relative_to(dist).as_posix():p.read_bytes() for p in dist.rglob("*") if p.is_file() and "__pycache__" not in p.parts and p.suffix!=".pyc"}
# Release sources come from the exact committed revision, including its notices.
# Unrelated local edits and untracked files must not enter the source bundle.
commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=root,text=True).strip()
source_archive=subprocess.check_output(["git","archive","--format=zip",commit],cwd=root)
with zipfile.ZipFile(io.BytesIO(source_archive)) as source:
    for name in source.namelist():
        if not name.endswith("/"):
            payload=source.read(name)
            if name.startswith("backend/"):
                assert selected.get(name)==payload, f"Installed backend differs from release commit: {name}"
            selected[name]=payload

manifest={"format":1,"application":"AoE2DE AI Replay Analyzer","date":date.today().isoformat(),
    "source_commit":commit,
    "replay_payloads_included":False,"python_runtime_included":False,
    "pipeline_sha256":fingerprint(dist/"backend"/"vendor",dist/"backend"/"vendor_helpers"),
    "files":{name:hashlib.sha256(payload).hexdigest() for name,payload in sorted(selected.items())}}
with zipfile.ZipFile(destination,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for name,payload in sorted(selected.items()):z.writestr(name,payload)
    z.writestr("CERTIFICATION.json",json.dumps(manifest,indent=2))
with zipfile.ZipFile(destination) as z:
    assert z.testzip() is None
    assert "replay-analysis.exe" in z.namelist() and "src/main.cpp" in z.namelist()
    assert not any(name.endswith((".aoe2record",".sqlite",".partial")) for name in z.namelist())
print(json.dumps({"package":str(destination),"bytes":destination.stat().st_size,
    "sha256":hashlib.sha256(destination.read_bytes()).hexdigest(),"files":len(selected)},indent=2))
