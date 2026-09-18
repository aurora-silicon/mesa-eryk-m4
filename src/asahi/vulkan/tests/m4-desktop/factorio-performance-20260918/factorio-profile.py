#!/usr/bin/env python3
"""Profile an existing Factorio VM for a bounded interval inside gpu-experiment."""
import argparse
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
p.add_argument('--seconds', type=int, default=12)
p.add_argument('--toggle-fps', action='store_true')
a = p.parse_args()
assert 1 <= a.seconds <= 20
guard = json.loads((a.output / 'start.json').read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds'] == 60
subprocess.run(['systemctl', 'is-active', '--quiet', 'm4-factorio-standalone.service'], check=True)
desktop = ['python3', '/root/steam-desktop-command.py']
guest = desktop + ['/usr/bin/muvm', '--privileged', '-i', '--']
with (a.output / 'guest-before.txt').open('w') as f:
    subprocess.run(guest + ['/usr/bin/ps', '-eLo', 'pid,tid,pcpu,stat,wchan:32,comm'], stdout=f, check=True, timeout=10)
if a.toggle_fps:
    subprocess.run(['python3', '/root/factorio-ui-step.py', 'key', 'F5'], check=True, timeout=10)
env = os.environ.copy()
env.update(LD_LIBRARY_PATH='/opt/m4-profiling/usr/lib', DEBUGINFOD_URLS='')
perf = ['/opt/m4-profiling/usr/bin/perf', 'record', '-a', '-e', 'cpu-clock', '-F', '99', '-g', '-o']
guest_data = Path('/home/e/.local/share/factorio-standalone/demo-2.0.77/current-profile.data')
children = []
try:
    with (a.output / 'perf.log').open('w') as f:
        children.append(subprocess.Popen(perf + [str(a.output / 'host.perf.data'), '--', '/usr/bin/sleep', str(a.seconds)], env=env, stdout=f, stderr=f))
        children.append(subprocess.Popen(guest + ['/usr/bin/env', 'LD_LIBRARY_PATH=/opt/m4-profiling/usr/lib', 'DEBUGINFOD_URLS='] + perf + [str(guest_data), '--', '/usr/bin/sleep', str(a.seconds)], stdout=f, stderr=f))
        for child in children:
            assert child.wait(timeout=a.seconds + 8) == 0
    import shutil
    shutil.copyfile(guest_data, a.output / 'guest.perf.data')
    with (a.output / 'screen.png').open('wb') as f:
        subprocess.run(desktop + ['/usr/bin/grim', '-o', 'eDP-1', '-'], stdout=f, check=True, timeout=10)
    print('FACTORIO_PROFILE_COLLECTED', flush=True)
finally:
    for child in children:
        if child.poll() is None:
            child.terminate()
    for child in children:
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
