#!/usr/bin/env python3
"""Recover the J713 user's Xwayland under gpu-experiment's existing guard.

Temporarily disables Xwayland in the root session wrapper and restores its
exact bytes in finally. Leaves Wayland clients and the user's session intact.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--guard-output', required=True, type=Path)
a = p.parse_args()
spec = importlib.util.spec_from_file_location('fw', '/root/firmware-snapshot.py')
fw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fw)
fw.check_guard(a.guard_output, Path('/proc/sys/kernel/random/boot_id').read_text().strip())
config = Path('/usr/local/share/m4-desktop/session.lua')
original = config.read_bytes()
(a.guard_output / 'session.lua.before').write_bytes(original)
env = dict(os.environ, XDG_RUNTIME_DIR='/run/user/1000')
def ctl(*args):
    return subprocess.run(['runuser', '-u', 'e', '--', 'hyprctl', '-i', '0', *args],
                          env=env, text=True, capture_output=True, check=True, timeout=4)
def interrupt(signum, frame):
    raise InterruptedError(signum)
signal.signal(signal.SIGTERM, interrupt)
signal.signal(signal.SIGINT, interrupt)
try:
    config.write_bytes(original + b'\nhl.config({xwayland = {enabled = false}})\n')
    print(ctl('reload', 'config-only').stdout, flush=True)
    time.sleep(1)
finally:
    config.write_bytes(original)
    print(ctl('reload', 'config-only').stdout, flush=True)
time.sleep(2)
display = ctl('repl', 'os.getenv("DISPLAY")').stdout.strip()
assert display.startswith(':'), display
r = subprocess.run(['runuser', '-u', 'e', '--', 'xwininfo', '-display', display, '-root'],
                   env=env, text=True, capture_output=True, check=True, timeout=5)
print(r.stdout, flush=True)
assert config.read_bytes() == original
for proc in Path('/proc').glob('[0-9]*'):
    try:
        if (proc / 'comm').read_text().strip() == 'Xwayland':
            (a.guard_output / ('xwayland-' + proc.name + '-maps.txt')).write_text((proc / 'maps').read_text())
    except (FileNotFoundError, ProcessLookupError):
        pass
print('PASS: Xwayland recovered and answers X11 requests', flush=True)
