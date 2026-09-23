#!/usr/bin/env python3
"""Restart the normal desktop under gpu-experiment; verify Mesa and prior outputs.

Usage: restart-graphics-desktop.py GUARD_OUTPUT EXPECTED_VULKAN_SHA256 [LIBRARY]
Requires the existing e/UID1000 autologin. Terminates the current user session.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time

out = Path(sys.argv[1])
expected = sys.argv[2]
spec = importlib.util.spec_from_file_location('fw', '/root/firmware-snapshot.py')
fw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fw)
boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
fw.check_guard(out, boot)
library = Path(sys.argv[3] if len(sys.argv) > 3 else '/opt/asahi-m4-port/lib/libvulkan_asahi.so')
assert library.is_absolute()
assert hashlib.sha256(library.read_bytes()).hexdigest() == expected

def desktop():
    pids = subprocess.check_output(['pgrep', '-u', '1000', '-x', 'Hyprland'], text=True).split()
    assert len(pids) == 1, pids
    proc = Path('/proc') / pids[0]
    assert b'--safe-mode' not in (proc/'cmdline').read_bytes().split(b'\0')
    env = dict(x.decode().split('=', 1) for x in (proc/'environ').read_bytes().split(b'\0') if b'=' in x)
    sockets = list(Path('/run/user/1000/hypr').glob('*/.socket.sock'))
    assert len(sockets) == 1
    env['HYPRLAND_INSTANCE_SIGNATURE'] = sockets[0].parent.name
    monitors = json.loads(subprocess.check_output(['runuser', '-u', 'e', '--', 'hyprctl', '-j', 'monitors'], env=env, text=True, timeout=3))
    return proc, monitors

before, monitors = desktop()
required = {m['name'] for m in monitors if not m.get('disabled')}
assert required
(out/'monitors-before.json').write_text(json.dumps(monitors, indent=2)+'\n')
subprocess.run(['systemctl', 'restart', 'display-manager'], check=True, timeout=20)
deadline = time.monotonic() + 20
stable = None
while time.monotonic() < deadline:
    fw.check_guard(out, boot)
    try:
        proc, monitors = desktop()
        assert proc != before
        assert required <= {m['name'] for m in monitors if not m.get('disabled') and m.get('dpmsStatus') and m.get('width')}
        mappings = (proc/'maps').read_text()
        assert str(library) in mappings
        assert str(library)+' (deleted)' not in mappings
        stable = stable or time.monotonic()
        if time.monotonic() - stable >= 3:
            (out/'monitors-after.json').write_text(json.dumps(monitors, indent=2)+'\n')
            (out/'compositor-maps.txt').write_text(mappings)
            (out/'desktop.json').write_text(json.dumps(dict(pid=int(proc.name),library=str(library),sha256=expected,outputs=sorted(required)),indent=2)+'\n')
            print('PASS: normal desktop restarted with verified Mesa and prior displays', flush=True)
            break
    except (AssertionError, FileNotFoundError, ProcessLookupError, subprocess.SubprocessError):
        stable = None
    time.sleep(.5)
else:
    raise RuntimeError('Normal desktop did not restore its Mesa mapping and prior displays')
