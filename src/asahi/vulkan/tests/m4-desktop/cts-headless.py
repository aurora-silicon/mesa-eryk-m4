#!/usr/bin/env python3
"""Run a guarded CTS batch on one private 1024x1024@1000 Hyprland output.

Use one Vulkan WSI worker per compositor; qualified GL groups may share it.
The outer experiment guard owns all child
processes; this wrapper does not renew the watchdog or relax case deadlines.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--guard-output', required=True, type=Path)
p.add_argument('--env', action='append', default=[], metavar='KEY=VALUE')
p.add_argument('command', nargs=argparse.REMAINDER)
a = p.parse_args()
command = a.command[1:] if a.command[:1] == ['--'] else a.command
assert command and '--jobs' in command
cases = Path(command[command.index('--cases')+1]).read_text()
if command[command.index('--jobs')+1] != '1':
    assert all(c.startswith(('KHR-GL', 'KHR-NoContext.')) for c in cases.splitlines()), 'WSI needs one worker per compositor'
needs_x11 = '.wsi.xlib.' in cases or '.wsi.xcb.' in cases
spec = importlib.util.spec_from_file_location('fw', '/root/firmware-snapshot.py')
fw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fw)
boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
def check():
    fw.check_guard(a.guard_output, boot)
check()
out = a.guard_output/'headless'
out.mkdir()
runtime = Path('/run')/f'ct{os.getpid()}'
runtime.mkdir(mode=0o700)
env = None
for proc in Path('/proc').glob('[0-9]*'):
    try:
        argv = (proc/'cmdline').read_bytes()
        if b'/opt/m4-hyprland/bin/Hyprland' in argv and b'--watchdog-fd' in argv:
            env = dict(item.decode().split('=',1) for item in (proc/'environ').read_bytes().split(b'\0') if b'=' in item)
            break
    except OSError:
        pass
if env is None:
    # A dedicated CTS run can release the panel desktop's GPU allocations.
    # cts-desktop-control.py retains its renderer settings in root-owned /run;
    # only accept the same boot, and record this environment source explicitly.
    saved = Path('/run/m4-cts-renderer-environment.json')
    assert saved.stat().st_uid == 0 and saved.stat().st_mode & 0o077 == 0
    snapshot = json.loads(saved.read_text())
    assert snapshot['boot_id'] == boot, 'Saved renderer environment is from another boot'
    env = snapshot['environment']
    (out/'saved-renderer-environment.json').write_text(json.dumps(snapshot, indent=2)+'\n')
assert env is not None, 'Need the qualified desktop renderer environment'
for k in list(env):
    if k.startswith(('HYPRLAND_', 'XDG_', 'DBUS_', 'INVOCATION', 'JOURNAL', 'LISTEN', 'NOTIFY', 'AQ_DRM')) or k in ('WAYLAND_DISPLAY','DISPLAY','HOME','USER','LOGNAME','PWD','SHLVL','_'):
        env.pop(k)
env.update(XDG_RUNTIME_DIR=str(runtime), HOME='/root', USER='root',
           AQ_HEADLESS_RENDER_NODE='/dev/dri/renderD128', HYPRLAND_NO_SD_NOTIFY='1', AQ_USE_RENDER_NODE='1',
           # Desktop drirc requests high priority for Hyprland. At 1000 Hz that
           # can permanently starve the normal-priority CTS client's first job.
           # Match the client priority for this synthetic pacing environment.
           zink_queue_priority='1')
overrides = dict(item.split('=', 1) for item in a.env)
env.update(overrides)
(out/'renderer-overrides.json').write_text(json.dumps({'zink_queue_priority': env['zink_queue_priority'], **overrides}, indent=2)+'\n')
config = out/'headless.lua'
config.write_text('''-- CTS windows must be able to choose and change their pixel dimensions.
hl.window_rule({ match = { class = ".*" }, float = true })
hl.monitor({ output = "HEADLESS-1", mode = "1024x1024@1000", position = "0x0", scale = 1 })
hl.config({
  misc = { disable_hyprland_logo = true, disable_splash_rendering = true, force_default_wallpaper = 0 },
  general = { gaps_in = 0, gaps_out = 0, border_size = 0 },
  decoration = { rounding = 0, shadow = { enabled = false }, blur = { enabled = false } },
  animations = { enabled = false },
  debug = { disable_logs = false, enable_stdout_logs = true },
})
''')
hypr = None
child = None
def interrupted(signum, frame):
    raise InterruptedError(f'signal {signum}')
signal.signal(signal.SIGTERM, interrupted)
signal.signal(signal.SIGINT, interrupted)
try:
    with (out/'hyprland.log').open('w') as log:
        hypr = subprocess.Popen(['/opt/m4-hyprland/bin/Hyprland','--i-am-really-stupid','--config',str(config)],
                                env=env,cwd='/root',stdout=log,stderr=subprocess.STDOUT)
        deadline = time.monotonic()+15
        sock = None
        while time.monotonic() < deadline:
            check()
            assert hypr.poll() is None, 'Headless compositor exited'
            socks = [s for s in runtime.glob('wayland-*') if s.is_socket()]
            log_text = (out/'hyprland.log').read_text(errors='replace')
            if (socks and 'using available mode 1024x1024@1000.00Hz' in log_text
                    and (not needs_x11 or 'XWayland is ready' in log_text)):
                sock = socks[0]
                break
            time.sleep(.2)
        assert sock, 'Qualified 1000 Hz headless output unavailable'
        # Xwayland is this compositor's child; never select the panel's server.
        xenv = {}
        for proc in Path('/proc').glob('[0-9]*'):
            try:
                if (proc/'comm').read_text().strip() != 'Xwayland':
                    continue
                fields = dict(line.split(':',1) for line in (proc/'status').read_text().splitlines() if ':' in line)
                if int(fields['PPid']) != hypr.pid:
                    continue
                argv = (proc/'cmdline').read_bytes().decode().strip('\0').split('\0')
                xenv['DISPLAY'] = next(s for s in argv if s.startswith(':') and s[1:].isdigit())
                if '-auth' in argv:
                    xenv['XAUTHORITY'] = argv[argv.index('-auth')+1]
            except (OSError,StopIteration):
                pass
        if needs_x11:
            assert 'DISPLAY' in xenv, 'Private Xwayland unavailable'
        selected = dict(XDG_RUNTIME_DIR=str(runtime), WAYLAND_DISPLAY=sock.name,
                        DISPLAY=xenv.get('DISPLAY',''), XAUTHORITY=xenv.get('XAUTHORITY',''))
        (out/'environment.json').write_text(json.dumps(selected,indent=2)+'\n')
        for k,v in selected.items():
            command += ['--env',k+'='+v]
        child = subprocess.Popen(command)
        single_case = cases.strip().splitlines()
        binary = Path(command[command.index('--binary')+1]).resolve()
        started_record = False
        while child.poll() is None:
            check()
            assert hypr.poll() is None, 'Headless compositor died during CTS'
            if len(single_case) == 1 and not started_record:
                for proc in Path('/proc').glob('[0-9]*'):
                    try:
                        pid = int(proc.name)
                        if os.getpgid(pid) != os.getpgrp() or (proc/'exe').resolve() != binary:
                            continue
                        record = dict(case=single_case[0], pid=pid, boot_id=boot,
                                      monotonic_seconds=time.monotonic(),
                                      argv=(proc/'cmdline').read_bytes().decode().split('\0'))
                        with (out/'single-case-process.json').open('w') as stream:
                            json.dump(record, stream); stream.flush(); os.fsync(stream.fileno())
                        fd = os.open(out, os.O_RDONLY | os.O_DIRECTORY)
                        try: os.fsync(fd)
                        finally: os.close(fd)
                        started_record = True
                        break
                    except (OSError, ValueError):
                        pass
            time.sleep(.2)
        rc = child.returncode
finally:
    from cts_deqp_backend import kill_tree
    if child and child.poll() is None:
        kill_tree(child)
    if hypr and hypr.poll() is None:
        hypr.terminate()
        try:
            hypr.wait(timeout=3)
        except subprocess.TimeoutExpired:
            kill_tree(hypr)
    shutil.rmtree(runtime)
raise SystemExit(rc)
