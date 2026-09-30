#!/usr/bin/env python3
"""Bounded Minecraft OpenGL gameplay witness under the existing M3 guard.

Use a separately copied world/settings directory, not the user's live world.
The host stages the existing maximize/window-shot helpers alongside this file.
This launches no capture layer and supplies no benchmark score. Keep the guard's
60-second watchdog, thermal monitoring and retirement checks enabled.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time
import traceback

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
p.add_argument('--prefix', required=True, type=Path)
p.add_argument('--game-dir', required=True, type=Path)
p.add_argument('--worker-policy', choices=('default', 'false', 'true'), default='default')
p.add_argument('--native-backtrace', action='store_true', help='Bounded startup-only gdb snapshot')
p.add_argument('--presentation', choices=('x11', 'wayland'), default='x11')
a = p.parse_args()
assert 'guard.py' in Path('/proc', str(os.getppid()), 'cmdline').read_text()
guard = json.loads((a.output / 'result.json').read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds'] == 60
assert a.game_dir.is_dir() and '.cache' in a.game_dir.parts
assert not (a.prefix / 'CANDIDATE_INCOMPLETE').exists()
pid = int(subprocess.check_output(['pgrep', '-u', 'eryk', '-x', 'plasmashell'], text=True).split()[0])
env = dict(os.environ)
for key in list(env):
    if key.startswith(('VK_', 'HK_', 'AGX_', 'ZINK_', 'MESA_', 'GFXRECON_', 'APITRACE_')) or key == 'LD_PRELOAD':
        env.pop(key)
desktop = dict(part.decode().split('=', 1) for part in
               Path('/proc', str(pid), 'environ').read_bytes().split(b'\0') if b'=' in part)
for key in ('DISPLAY', 'XAUTHORITY', 'WAYLAND_DISPLAY', 'XDG_RUNTIME_DIR', 'DBUS_SESSION_BUS_ADDRESS'):
    if key in desktop:
        env[key] = desktop[key]
assert env.get('DISPLAY') and env.get('XAUTHORITY')
if a.presentation == 'x11':
    env.pop('WAYLAND_DISPLAY', None)
else:
    assert env.get('WAYLAND_DISPLAY')
env.update(XDG_SESSION_TYPE=a.presentation, SDL_VIDEODRIVER=a.presentation, SDL_VIDEO_DRIVER=a.presentation,
           LD_LIBRARY_PATH=str(a.prefix / 'lib'),
           LIBGL_DRIVERS_PATH=str(a.prefix / 'lib/dri'),
           GBM_BACKENDS_PATH=str(a.prefix / 'lib/gbm'),
           VK_DRIVER_FILES=str(a.prefix / 'asahi.json'),
           __EGL_VENDOR_LIBRARY_FILENAMES=str(a.prefix / 'egl.json'),
           MESA_LOADER_DRIVER_OVERRIDE='zink', GALLIUM_DRIVER='zink',
           ASAHI_M3_EXPERIMENTAL='1', ASAHI_M4_EXPERIMENTAL='1',
           HK_DEBUG_LOG='0', AGX_MESA_DEBUG='nopromote')
if a.worker_policy != 'default':
    env['ZINK_PREFER_HIGH_CAPACITY_CPUS'] = a.worker_policy
prism = Path('/home/eryk/.local/share/PrismLauncher')
java = Path('/usr/lib/jvm/java-25-openjdk/bin/java')
cmd = ['runuser', '-u', 'eryk', '--', str(java), '-Xms512m', '-Xmx4096m',
       '-XX:StackShadowPages=32', '-cp', ':'.join(str(f) for f in sorted((prism / 'libraries').rglob('*.jar'))),
       'net.minecraft.client.main.Main', '--demo', '--username', 'Player', '--version', '26.3',
       '--gameDir', str(a.game_dir), '--assetsDir', str(prism / 'assets'), '--assetIndex', '34',
       '--uuid', '00000000000000000000000000000000', '--accessToken', '0',
       '--versionType', 'release', '--width', '1779', '--height', '1081',
       '--graphicsBackend', 'opengl', '--quickPlaySingleplayer', 'Demo_World']
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
receipt = dict(boot_id=guard['boot_id'], diagnostic_only=True,
               scope='Bounded real Minecraft demo gameplay; no capture or FPS score',
               prefix=str(a.prefix), command=cmd,
               presentation=a.presentation,
               mesa_sha256={f.name: sha(f) for f in
                            [a.prefix / 'lib/libvulkan_asahi.so', a.prefix / 'lib/libgallium-26.1.4.so']},
               java_sha256=sha(java), options_sha256=sha(a.game_dir / 'options.txt'),
               environment={k: v for k, v in env.items() if k.startswith(
                   ('LD_', 'VK_', 'MESA_', 'GALLIUM_', 'HK_', 'AGX_', 'ZINK_', 'ASAHI_', 'SDL_', 'LIBGL_', '__EGL_'))})
(a.output / 'game-manifest.json').write_text(json.dumps(receipt, indent=2) + '\n')
before = subprocess.check_output(['dmesg', '--color=never'], text=True)
child = None
result = dict(success=False)
start = time.monotonic()

def java_child_pid():
    selected = subprocess.check_output(['pgrep', '-u', 'eryk', '-f',
                                       r'net\.minecraft\.client\.main\.Main'], text=True).split()
    assert len(selected) == 1, 'Run the witness with no other Minecraft instance'
    return int(selected[0])

try:
    with (a.output / 'game.log').open('w') as log:
        child = subprocess.Popen(cmd, env=env, cwd=a.game_dir, stdout=log,
                                 stderr=subprocess.STDOUT, start_new_session=True)
        startup_recorded = False
        while 'joined the game' not in (a.output / 'game.log').read_text(errors='replace'):
            assert child.poll() is None, 'Minecraft exited before entering gameplay'
            if not startup_recorded and time.monotonic() - start > 10:
                startup_recorded = True
                tree = subprocess.run(['runuser', '-u', 'eryk', '--', 'xwininfo', '-root', '-tree'],
                                      env=env, text=True, capture_output=True, timeout=3)
                (a.output / 'startup-x11-tree.txt').write_text(tree.stdout + tree.stderr)
                java_pid = java_child_pid()
                maps = Path('/proc', str(java_pid), 'maps').read_text()
                (a.output / 'startup-mesa-maps.txt').write_text('\n'.join(
                    line for line in maps.splitlines() if '/opt/asahi-' in line))
                threads = []
                for task in Path('/proc', str(java_pid), 'task').iterdir():
                    try:
                        threads.append(dict(tid=int(task.name), comm=(task / 'comm').read_text().strip(),
                                            wchan=(task / 'wchan').read_text().strip(),
                                            status=(task / 'status').read_text()))
                    except FileNotFoundError:
                        pass
                (a.output / 'startup-threads.json').write_text(json.dumps(threads, indent=2) + '\n')
                tgid = re.search(r'^Tgid:\s*(\d+)', Path('/proc', str(java_pid), 'status').read_text(), re.M)[1]
                # HotSpot's SIGQUIT handler writes its thread dump to game.log;
                # the installed JRE does not include jcmd. No process is stopped.
                os.kill(int(tgid), signal.SIGQUIT)
                if a.native_backtrace:
                    with (a.output / 'startup-native-threads.txt').open('w') as backtrace:
                        subprocess.run(['gdb', '-q', '-batch', '-p', tgid,
                                        '-ex', 'set debuginfod enabled off',
                                        '-ex', 'thread apply all bt 12', '-ex', 'detach'],
                                       stdout=backtrace, stderr=subprocess.STDOUT, timeout=6)
                shot = a.game_dir.parent / 'startup.png'
                tree = subprocess.run(['runuser', '-u', 'eryk', '--', 'xwininfo', '-root', '-tree'],
                                      env=env, text=True, capture_output=True, timeout=3)
                (a.output / 'startup-x11-tree.txt').write_text(tree.stdout + tree.stderr)
                capture = subprocess.run(['runuser', '-u', 'eryk', '--', 'python3',
                                          str(Path(__file__).parent / 'minecraft-window-shot.py'), str(shot)],
                                         env=env, capture_output=True, text=True, timeout=3)
                (a.output / 'startup-shot.log').write_text(capture.stdout + capture.stderr)
                if capture.returncode == 0:
                    (a.output / 'startup.png').write_bytes(shot.read_bytes())
                if a.presentation == 'wayland':
                    from screenshot import capture as screenshot
                    with (a.output / 'startup-spectacle.log').open('w') as shotlog:
                        screenshot(env, a.output / 'startup.png', shotlog)
            assert time.monotonic() - start < 28, 'Gameplay startup exceeded the bounded witness'
            time.sleep(.2)
        result['world_ready_seconds'] = time.monotonic() - start
        java_pid = java_child_pid()
        maps = Path('/proc', str(java_pid), 'maps').read_text()
        selected = [line for line in maps.splitlines() if str(a.prefix / 'lib/libgallium') in line]
        assert selected, 'Minecraft did not load the candidate Gallium library'
        result['candidate_library_maps'] = selected
        helpers = Path(__file__).parent
        if a.presentation == 'x11':
            result['window'] = subprocess.check_output(
                ['runuser', '-u', 'eryk', '--', 'python3', str(helpers / 'minecraft-maximize.py')],
                env=env, text=True, timeout=5)
        time.sleep(5)
        assert child.poll() is None, 'Minecraft exited during gameplay'
        shot = a.game_dir.parent / 'live.png'
        command = ['python3', str(helpers / 'minecraft-window-shot.py'), str(shot)] if a.presentation == 'x11' else [
            'spectacle', '-b', '-n', '-f', '-o', str(shot)]
        subprocess.run(['runuser', '-u', 'eryk', '--', *command],
                       env=env, check=True, timeout=5)
        (a.output / 'live.png').write_bytes(shot.read_bytes())
        result['success'] = True
except Exception as ex:
    traceback.print_exc()
    result['error'] = repr(ex)
finally:
    if child and child.poll() is None:
        os.killpg(child.pid, signal.SIGTERM)
        try:
            child.wait(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait(timeout=2)
    result['elapsed_seconds'] = time.monotonic() - start
    (a.output / 'game-result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)
after = subprocess.check_output(['dmesg', '--color=never'], text=True)
delta = after[len(before):] if after.startswith(before) else after
(a.output / 'kernel-delta.log').write_text(delta)
assert not re.search(r'execution failed|GPU fault|firmware crashed|BUG:|Oops:|GPU retirement unproven', delta), 'GPU fault; evidence retained'
