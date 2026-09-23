#!/usr/bin/env python3
"""Atomically install the qualified screenshot clients; preserve packaged tools.

Build with build-hyprpicker.sh and the documented slurp cross build first.
Installs /usr/local/bin overrides, verifies hashes, and retains any prior override.
Does not restart the desktop or launch a graphics workload.
"""

import argparse
import hashlib
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument(
    "--compositor",
    action="store_true",
    help="Install the built Hyprland; restart the session separately under the guard",
)
p.add_argument("--backend", action="store_true", help="Install the built Aquamarine library")
a = p.parse_args()
if a.compositor and a.backend:
    p.error("select --compositor or --backend")
root = Path(__file__).resolve().parent.parent
entries = (
    ("hyprpicker", root / "build/hyprpicker-m4-arm64/hyprpicker", "/usr/local/bin/hyprpicker", "755"),
    ("slurp", root / "build/slurp-m4-arm64/slurp", "/usr/local/bin/slurp", "755"),
)
if a.compositor:
    entries = (("Hyprland", root / "build/hyprland-m4-arm64/Hyprland", "/opt/m4-hyprland/bin/Hyprland", "755"),)
if a.backend:
    entries = (("aquamarine", root / "build/aquamarine-m4-arm64/libaquamarine.so.0.14.0", "/opt/m4-hyprland/lib/libaquamarine.so.13", "755"),)
for name, binary, destination, mode in entries:
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    staged = f"/root/{name}-{digest}"
    subprocess.run(["scp", "-q", str(binary), "root@omarchy-m4:" + staged], check=True)
    script = """from pathlib import Path
import hashlib,json,os,shutil,sys
name, staged, digest, destination, mode = sys.argv[1:]
source=Path(staged)
assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
target=Path(destination)
target.parent.mkdir(parents=True,exist_ok=True)
previous=None
if target.exists():
    previous=hashlib.sha256(target.read_bytes()).hexdigest()
    backup=Path('/opt/m4-screenshot-backups')/(name+'-'+previous)
    backup.parent.mkdir(parents=True,exist_ok=True)
    if not backup.exists(): shutil.copy2(target,backup)
temporary=target.with_suffix('.new')
shutil.copy2(source,temporary);temporary.chmod(int(mode,8))
assert hashlib.sha256(temporary.read_bytes()).hexdigest()==digest
with temporary.open('rb') as stream: os.fsync(stream.fileno())
os.replace(temporary,target)
record=dict(path=str(target),sha256=digest,previous_override_sha256=previous,packaged_path='/usr/bin/'+name if name in ('slurp','hyprpicker') else None)
manifest=Path('/opt/m4-screenshot-backups')/(name+'-installed.json')
manifest.parent.mkdir(parents=True,exist_ok=True)
manifest.write_text(json.dumps(record,indent=2)+'\\n')
print(json.dumps(record))
"""
    subprocess.run(
        [
            "ssh",
            "root@omarchy-m4",
            shlex.join(["python3", "-c", script, name, staged, digest, destination, mode]),
        ],
        check=True,
    )
