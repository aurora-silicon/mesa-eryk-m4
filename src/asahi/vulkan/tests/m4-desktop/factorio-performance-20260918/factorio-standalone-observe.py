#!/usr/bin/env python3
"""Launch standalone Factorio under gpu-experiment's watchdog/thermal guard.

Run on the target as root. Leave the ordinary application open only after
Factorio reports initialization and remains alive; stop its VM on failure.
The game's settings/logs/saves live in a separate, explicitly supplied directory.
"""
import argparse
import json
import os
import math
import statistics
from pathlib import Path
import signal
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--state', type=Path, required=True)
p.add_argument('--launcher', required=True)
p.add_argument('--seconds', type=int, default=480)
p.add_argument('--observe-seconds', type=int, default=0, help='After initialization, measure this long then stop the benchmark service')
p.add_argument('--fps-log', type=Path, help='Retain a fresh Gallium invisible-HUD fps file after bounded observation')
p.add_argument('--screenshot', action='store_true', help='Explicitly request an initial visual reference')
a = p.parse_args()
assert 10 <= a.seconds <= 900
assert not a.fps_log or a.observe_seconds >= 10
guard = json.loads((a.output/'start.json').read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds'] == 60
assert int(os.environ['M4_TEST_CPU_MAX_KHZ']) == 4104000

def vms():
    found = []
    for proc in Path('/proc').glob('[0-9]*'):
        try:
            argv = (proc/'cmdline').read_bytes().split(b'\0')
            if argv[0] == b'/usr/bin/muvm' and b'-f' in argv:
                found.append(int(proc.name))
        except OSError:
            pass
    return found

assert not vms(), 'An existing microVM is running; do not disturb it'
clocks = {}
for policy in Path('/sys/devices/system/cpu/cpufreq').glob('policy*'):
    clocks[policy.name] = (policy/'scaling_max_freq').read_text().strip()
    (policy/'scaling_max_freq').write_text((policy/'cpuinfo_max_freq').read_text())
(a.output/'cpu-limits-before.json').write_text(json.dumps(clocks))

def interrupted(signum, frame):
    raise InterruptedError(f'guard signal {signum}')
signal.signal(signal.SIGTERM, interrupted)
signal.signal(signal.SIGINT, interrupted)

success = False
child = None
unit = 'm4-factorio-standalone.service'
start = time.monotonic()
wall = time.time()
game_log = a.state/'factorio-current.log'
try:
    with (a.output/'factorio-launch.log').open('w') as log:
        child = subprocess.Popen(['systemd-run', '--collect', '--wait',
            '--unit='+unit, '--property=Type=exec',
            '--property=StandardOutput=append:'+str(a.output/'factorio-service.log'),
            '--property=StandardError=inherit',
            '/usr/bin/python3', '/root/steam-desktop-command.py', a.launcher],
            stdin=subprocess.DEVNULL, stdout=log, stderr=log, start_new_session=True)
    last = ''
    while time.monotonic()-start < a.seconds:
        assert child.poll() is None, f'Factorio launcher exited: {child.returncode}'
        if game_log.exists() and game_log.stat().st_mtime >= wall:
            text = game_log.read_text(errors='replace')
            (a.output/'factorio-current.log').write_text(text)
            if text.splitlines() and text.splitlines()[-1] != last:
                last = text.splitlines()[-1]
                print(last, flush=True)
            if any(s in text for s in ('Error CrashHandler', 'Segmentation fault',
                                       'Failed to create OpenGL')):
                raise RuntimeError('Factorio reported a startup failure')
            if 'Factorio initialised' in text:
                time.sleep(3)
                assert child.poll() is None, 'Factorio exited just after initialization'
                desktop = ['python3', '/root/steam-desktop-command.py']
                if a.screenshot:
                    with (a.output/'factorio-menu.png').open('wb') as image:
                        subprocess.run(desktop + ['/usr/bin/grim', '-o', 'eDP-1', '-'],
                            stdout=image, check=True, timeout=10)
                if a.observe_seconds:
                    assert 0 < a.observe_seconds <= 120
                    deadline = time.monotonic() + a.observe_seconds
                    while time.monotonic() < deadline:
                        assert child.poll() is None, 'Factorio exited during timing'
                        time.sleep(.5)
                subprocess.run(['systemctl', 'is-active', '--quiet', unit], check=True)
                assert child.poll() is None, 'Factorio exited during final observation'
                result = dict(initialized=True, launcher_elapsed_seconds=time.monotonic()-start,
                              vm_pids=vms(), launcher=a.launcher, state=str(a.state), unit=unit)
                if a.observe_seconds:
                    subprocess.run(['systemctl', 'stop', unit], check=True, timeout=15)
                if a.fps_log:
                    assert a.fps_log.stat().st_mtime >= wall, 'Stale FPS log'
                    fps_text = a.fps_log.read_text()
                    (a.output/'menu-fps.txt').write_text(fps_text)
                    samples = [float(line) for line in fps_text.splitlines()]
                    assert len(samples) >= 12 and all(math.isfinite(x) and x >= 0 for x in samples)
                    tail = samples[-12:]
                    result['menu_fps'] = dict(source='Gallium invisible HUD',
                        tail_samples=len(tail), mean=statistics.mean(tail),
                        minimum=min(tail), maximum=max(tail))
                (a.output/'factorio-result.json').write_text(json.dumps(result, indent=2))
                success = True
                print('FACTORIO_STANDALONE_INITIALISED', json.dumps(result), flush=True)
                break
        time.sleep(1)
    if not success:
        subprocess.run(['systemctl', 'stop', unit], timeout=10)
        raise RuntimeError('Standalone startup exceeded its bound')
finally:
    if not success:
        for pid in vms():
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        if child and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
