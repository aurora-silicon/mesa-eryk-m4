#!/usr/bin/env python3
"""Restart SDDM and verify ordinary dual-output startup under gpu-experiment."""
import json
from pathlib import Path
import subprocess
import sys
import time

out = Path(sys.argv[1])
report = {'samples': []}
params = Path('/sys/module/m4_dcpext_desktop/parameters')
subprocess.run(['systemctl', 'restart', 'sddm'], check=True, timeout=15)
deadline = time.monotonic() + 20
stable = None
try:
    while time.monotonic() < deadline:
        assert (params/'native_kms_worker_done').read_text().strip() == 'N'
        for process in Path('/proc').glob('[0-9]*'):
            try:
                command = (process/'cmdline').read_bytes().split(b'\0')
                if command[0] != b'/opt/m4-hyprland/bin/Hyprland':
                    continue
                assert b'--safe-mode' not in command, command
                maps = (process/'maps').read_text()
                assert '/opt/m4-hyprland/lib/libaquamarine.so.13' in maps
                assert 'libaquamarine.so.13 (deleted)' not in maps
                env = dict(x.split('=', 1) for x in (process/'environ').read_text().split('\0') if '=' in x)
                for sock in (Path(env['XDG_RUNTIME_DIR'])/'hypr').glob('*/.socket.sock'):
                    env['HYPRLAND_INSTANCE_SIGNATURE'] = sock.parent.name
                    response = subprocess.run(['hyprctl', '-j', 'monitors'], env=env, capture_output=True, text=True, timeout=1)
                    if response.returncode:
                        continue
                    monitors = json.loads(response.stdout)
                    names = {m['name'] for m in monitors if m.get('width') and not m.get('disabled')}
                    report['samples'].append({'pid': int(process.name), 'monitors': monitors})
                    if 'DP-1' in names and any(n.startswith('eDP-') for n in names):
                        stable = stable or time.monotonic()
                    else:
                        stable = None
            except (FileNotFoundError, ProcessLookupError, subprocess.TimeoutExpired):
                continue
        if stable and time.monotonic()-stable >= 5:
            report['passed'] = True
            print('PASS: normal Hyprland startup with repaired Aquamarine and both displays')
            break
        time.sleep(0.3)
    assert report.get('passed'), report
finally:
    (out/'restart.json').write_text(json.dumps(report, indent=2)+'\n')
