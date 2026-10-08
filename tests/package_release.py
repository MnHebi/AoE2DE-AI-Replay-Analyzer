"""Package the installed application plus rebuildable C++ sources; exclude evidence."""
import hashlib
from datetime import date
import json
from pathlib import Path
import sys
import zipfile

root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/"backend"))
from adapter import fingerprint
dist=root/"dist"
destination=root/"AoE2ReplayAnalysis-Windows-x64.zip"
selected={p.relative_to(dist).as_posix():p for p in dist.rglob("*") if p.is_file() and "__pycache__" not in p.parts and p.suffix!=".pyc"}
for directory in ("src","tests"):
    for p in (root/directory).rglob("*"):
        if p.is_file() and "__pycache__" not in p.parts and p.suffix!=".pyc":
            selected[p.relative_to(root).as_posix()]=p
for name in ("CMakeLists.txt",".gitignore",".gitattributes","AGENTS.md","HANDOFF.md"):
    selected[name]=root/name

manifest={"format":1,"application":"AoE2DE AI Replay Analyzer","date":date.today().isoformat(),
    "replay_payloads_included":False,"python_runtime_included":False,
    "pipeline_sha256":fingerprint(dist/"backend"/"vendor",dist/"backend"/"vendor_helpers"),
    "files":{name:hashlib.sha256(p.read_bytes()).hexdigest() for name,p in sorted(selected.items())}}
with zipfile.ZipFile(destination,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for name,p in sorted(selected.items()):z.write(p,name)
    z.writestr("CERTIFICATION.json",json.dumps(manifest,indent=2))
with zipfile.ZipFile(destination) as z:
    assert z.testzip() is None
    assert "replay-analysis.exe" in z.namelist() and "src/main.cpp" in z.namelist()
    assert not any(name.endswith((".aoe2record",".sqlite",".partial")) for name in z.namelist())
print(json.dumps({"package":str(destination),"bytes":destination.stat().st_size,
    "sha256":hashlib.sha256(destination.read_bytes()).hexdigest(),"files":len(selected)},indent=2))
