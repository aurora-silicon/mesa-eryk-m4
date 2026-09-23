#!/usr/bin/env python3
"""Run a command as e with the active desktop environment (target only).

Use inside gpu-experiment.py for launches or UI changes. Read-only process/log
queries do not launch a graphics workload. Never prints the environment.
"""
import os
from pathlib import Path
import stat
import sys

desktops = []
for proc in Path('/proc').glob('[0-9]*'):
    try:
        if proc.stat().st_uid == 1000 and (proc / 'exe').resolve().name == 'Hyprland':
            desktops.append(proc)
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        pass
assert len(desktops) == 1, 'Expected one user desktop'
env = dict(item.split('=', 1) for item in
           (desktops[0] / 'environ').read_text().split('\0') if '=' in item)
env.update(HOME='/home/e', USER='e', LOGNAME='e', XDG_RUNTIME_DIR='/run/user/1000')
for key in ('LD_PRELOAD', 'TRACE_FILE'):
    env.pop(key, None)
sockets = []
for candidate in Path('/run/user/1000/hypr').glob('*/.socket.sock'):
    try:
        # A crashed compositor can leave its socket behind. Match the lock
        # to the live desktop selected above instead of counting stale paths.
        if (candidate.parent / 'hyprland.lock').read_text().splitlines()[0] == desktops[0].name:
            sockets.append(candidate)
    except (OSError, IndexError):
        pass
assert len(sockets) == 1
env['HYPRLAND_INSTANCE_SIGNATURE'] = sockets[0].parent.name
wayland = [p for p in Path('/run/user/1000').glob('wayland-*')
           if stat.S_ISSOCK(p.stat().st_mode)]
assert len(wayland) == 1
env['WAYLAND_DISPLAY'] = wayland[0].name
for proc in Path('/proc').glob('[0-9]*'):
    try:
        argv = (proc / 'cmdline').read_bytes().split(b'\0')
        if proc.stat().st_uid == 1000 and argv[0] == b'Xwayland':
            env['DISPLAY'] = argv[1].decode()
    except (FileNotFoundError, ProcessLookupError, PermissionError):
        pass
os.chdir('/home/e')
os.execvpe('runuser', ['runuser', '-u', 'e', '--', *sys.argv[1:]], env)
