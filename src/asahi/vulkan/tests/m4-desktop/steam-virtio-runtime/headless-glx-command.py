#!/usr/bin/env python3
"""Run a command on a private GPU-backed Hyprland/Xwayland display.

Target-only; invoke inside gpu-experiment.py. The command must be bounded.
Keep compositor logs under --output; always stop our compositor afterwards.
"""
import argparse
import os
from pathlib import Path
import pwd
import shutil
import signal
import subprocess
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--seconds', type=int, default=28, choices=range(1, 1801))
p.add_argument('command', nargs=argparse.REMAINDER)
a = p.parse_args()
assert os.environ.get('M4_TEST_CPU_MAX_KHZ'), 'mandatory guard missing'
command = a.command[1:] if a.command[:1] == ['--'] else a.command
assert command
user = pwd.getpwnam('e')
runtime = Path(tempfile.mkdtemp(prefix='m4-glx-headless-'))
os.chown(runtime, user.pw_uid, user.pw_gid)
env = dict(os.environ)
for key in list(env):
    if key.startswith(('HYPRLAND_', 'XDG_', 'DBUS_', 'AQ_DRM', 'LD_PRELOAD')) or key in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY'):
        env.pop(key)
for line in Path('/etc/environment.d/90-omarchy-j713-rendering.conf').read_text().splitlines():
    if line and not line.startswith('#') and '=' in line:
        key, value = line.split('=', 1)
        env[key] = value
env.update(HOME=user.pw_dir, USER=user.pw_name, LOGNAME=user.pw_name,
           XDG_RUNTIME_DIR=str(runtime), AQ_HEADLESS_RENDER_NODE='/dev/dri/renderD128',
           AQ_USE_RENDER_NODE='1', HYPRLAND_NO_SD_NOTIFY='1',
           DBUS_SESSION_BUS_ADDRESS=f'unix:path={runtime}/bus')
config = runtime / 'headless.lua'
config.write_text('''hl.monitor({ output = "HEADLESS-1", mode = "1024x768@60", position = "0x0", scale = 1 })
hl.config({
 misc = { disable_hyprland_logo = true, disable_splash_rendering = true, force_default_wallpaper = 0 },
 decoration = { shadow = { enabled = false }, blur = { enabled = false } },
 animations = { enabled = false },
 debug = { disable_logs = false, enable_stdout_logs = true },
})
''')
def xsockets():
    return {f.name for f in Path('/tmp/.X11-unix').glob('X*') if f.name[1:].isdigit() and f.is_socket()}
before = xsockets()
proc = bus = None
def interrupt(signum, frame):
    raise InterruptedError(signum)
signal.signal(signal.SIGTERM, interrupt)
signal.signal(signal.SIGINT, interrupt)
try:
    with (a.output / 'headless-glx.log').open('w') as log:
        bus = subprocess.Popen(['runuser', '-u', user.pw_name, '--', 'dbus-daemon',
                                '--session', '--nofork', '--address=' + env['DBUS_SESSION_BUS_ADDRESS']],
                               env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        proc = subprocess.Popen(['runuser', '-u', user.pw_name, '--', '/opt/m4-hyprland/bin/Hyprland',
                                 '--config', str(config)], env=env, stdout=log,
                                stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            new = xsockets() - before
            if len(new) == 1:
                break
            assert proc.poll() is None, 'headless compositor exited'
            time.sleep(.1)
        else:
            raise RuntimeError('no private Xwayland socket')
        display = ':' + new.pop()[1:]
        wayland = next(f.name for f in runtime.glob('wayland-*') if f.is_socket())
        print('Private headless GPU display:', display, flush=True)
        result = subprocess.run(command, env=dict(env, DISPLAY=display, WAYLAND_DISPLAY=wayland), timeout=a.seconds)
        raise SystemExit(result.returncode)
finally:
    for proc in (proc, bus):
      if proc is not None:
        try:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=3)
        except ProcessLookupError:
            pass
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            proc.wait()
    shutil.rmtree(runtime)
