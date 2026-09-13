#!/usr/bin/env python3
"""Replay a saved Vulkan capture on host lavapipe and retain reference frames."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("trace", type=Path)
p.add_argument("--output", required=True, type=Path)
p.add_argument("--frames", required=True, help="GFXReconstruct frame selection")
p.add_argument("--seconds", type=int, default=60)
p.add_argument("--dump-config", type=Path)
p.add_argument("--remove-unsupported", action="store_true", help="Explicit diagnostic cross-device adaptation; not native qualification")
p.add_argument("--validation-layer", type=Path, help="Khronos validation JSON; retain layer hash and enable checks")
a = p.parse_args()
if platform.machine() != "x86_64" or not 1 <= a.seconds <= 120:
    p.error("this helper requires the x86 host and a 1..120 second bound")
trace = a.trace.resolve(strict=True)
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
icd = ROOT / "build/mesa-m4-reference/src/gallium/targets/lavapipe/lvp_devenv_icd.x86_64.json"
library = Path(json.loads(icd.read_text())["ICD"]["library_path"])
if not library.is_absolute():
    library = icd.parent / library
library = library.resolve(strict=True)
assert "libvulkan_lvp" in library.name
replay = ROOT / "build/gfxreconstruct-host/tools/replay/gfxrecon-replay"
env = {k: v for k, v in os.environ.items() if not k.startswith(
    ("VK_", "GFXRECON_", "MESA_", "AGX_", "ASAHI_", "HK_", "LD_"))}
env.update(VK_DRIVER_FILES=str(icd), LP_NUM_THREADS="4")
command = [str(replay), "--swapchain", "offscreen", "--memory-translation",
           "rebind", "--omit-pipeline-cache-data", "--screenshots", a.frames,
           "--screenshot-format", "png", "--screenshot-dir", str(out), str(trace)]
if a.remove_unsupported:
    command[-1:-1] = ["--remove-unsupported"]
validation_files = []
if a.validation_layer:
    layer = a.validation_layer.resolve(strict=True)
    description = json.loads(layer.read_text())
    assert description['layer']['name'] == 'VK_LAYER_KHRONOS_validation'
    layer_library = Path(description['layer']['library_path'])
    if not layer_library.is_absolute():
        layer_library = layer.parent / layer_library
    layer_library = layer_library.resolve(strict=True)
    description['layer']['library_path'] = str(layer_library)
    layer_dir = out / 'layers'
    layer_dir.mkdir()
    (layer_dir / layer.name).write_text(json.dumps(description, indent=2) + '\n')
    env['VK_LAYER_PATH'] = str(layer_dir)
    command[-1:-1] = ['--validate']
    validation_files = [layer, layer_library]
if a.dump_config:
    config = out / "dump-config.json"
    config.write_bytes(a.dump_config.resolve(strict=True).read_bytes())
    command[-1:-1] = ["--dump-resources", str(config),
                      "--dump-resources-dir", str(out)]
def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()
manifest = dict(command=command, renderer="host lavapipe", seconds=a.seconds,
                environment={k: env[k] for k in ("VK_DRIVER_FILES", "LP_NUM_THREADS", "VK_LAYER_PATH") if k in env},
                hashes={str(f): sha(f) for f in (trace, replay, icd, library, *validation_files)})
if a.dump_config:
    manifest["hashes"][str(config)] = sha(config)
(out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
start = time.monotonic()
try:
    with (out / "replay.log").open("w") as log:
        result = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                timeout=a.seconds)
except subprocess.TimeoutExpired:
    (out / "result.json").write_text(json.dumps(dict(returncode=None, timed_out=True,
        seconds=time.monotonic() - start), indent=2) + "\n")
    raise SystemExit(f"FAIL: software replay exceeded {a.seconds}s: {out}")
(out / "result.json").write_text(json.dumps(dict(returncode=result.returncode,
    timed_out=False, seconds=time.monotonic() - start), indent=2) + "\n")
result.check_returncode()
print("PASS: software replay completed:", out)
