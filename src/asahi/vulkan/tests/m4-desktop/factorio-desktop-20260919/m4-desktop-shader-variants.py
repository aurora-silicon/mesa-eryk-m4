#!/usr/bin/env python3
"""Enable tested M4 Mesa shader variants for the normal desktop under the guard.

Run on the target with gpu-experiment.py; backs up the graphics environment,
restarts SDDM with its existing autologin and verifies the new compositor's
inherited flags. On failure restores the environment and display manager.
Does not change game/compositor visual settings. --compression opts into the
already qualified M4 lossless render-target compression path.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
parser.add_argument('--compression', action='store_true')
args = parser.parse_args()
out = args.output
guard = json.loads((out / 'start.json').read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds'] == 60
path = Path('/etc/environment.d/90-omarchy-j713-rendering.conf')
original = path.read_bytes()
(out / 'desktop-environment.before').write_bytes(original)
flags = dict(HK_M4_PREFETCH_UBOS='true', HK_M4_PREFETCH_VARIANT='true',
             HK_M4_PLAIN_SAMPLERS='true', HK_M4_IOTO_TEMPLATES='true')
if args.compression:
    flags.update(ASAHI_MESA_DEBUG='', HK_M4_COMPRESSED_2D='true',
                 HK_M4_COMPRESSED_FEEDBACK='true', HK_M4_COMPRESSION_AUDIT='true')
    # Preserve unrelated debug options if the existing environment has any.
    previous_debug = next((line.partition('=')[2] for line in
                           original.decode().splitlines()
                           if line.startswith('ASAHI_MESA_DEBUG=')), '')
    flags['ASAHI_MESA_DEBUG'] = ','.join(flag for flag in previous_debug.split(',')
                                      if flag and flag != 'nocompress')
lines = [line for line in original.decode().splitlines()
         if line.partition('=')[0] not in flags]
updated = ('\n'.join(lines) + '\n' +
           ''.join(f'{k}={v}\n' for k,v in flags.items())).encode()
def replace(data):
    temp = path.with_suffix('.stage')
    temp.write_bytes(data)
    temp.chmod(path.stat().st_mode & 0o777)
    temp.replace(path)
try:
    replace(updated)
    subprocess.run(['systemctl', 'restart', 'display-manager'], check=True, timeout=20)
    deadline = time.monotonic() + 18
    while time.monotonic() < deadline:
        for proc in Path('/proc').glob('[0-9]*'):
            try:
                if proc.stat().st_uid != 1000 or (proc/'comm').read_text().strip() != 'Hyprland':
                    continue
                env = dict(item.split('=',1) for item in (proc/'environ').read_text().split('\0') if '=' in item)
                if all(env.get(k) == v for k,v in flags.items()):
                    (out/'desktop-variants.json').write_text(json.dumps(dict(pid=int(proc.name),flags=flags),indent=2))
                    print('DESKTOP_SHADER_VARIANTS_ACTIVE',flush=True)
                    raise SystemExit(0)
            except (FileNotFoundError,ProcessLookupError,PermissionError):
                pass
        time.sleep(.25)
    raise RuntimeError('New autologin compositor did not inherit flags')
except Exception:
    replace(original)
    subprocess.run(['systemctl', 'restart', 'display-manager'], timeout=20)
    raise
