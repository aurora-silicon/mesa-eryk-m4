#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded real-Hyprland probe; invoke only inside the armed render runner."""
import json
import re
import array
import hashlib
import mmap
import struct
import os
import pwd
from pathlib import Path
import signal
import subprocess
import time


def verify_scanout(result, width, height):
    frames = list(Path('/sys/firmware/devicetree/base/chosen').glob('framebuffer@*'))
    if len(frames) != 1:
        raise RuntimeError('Expected one retained panel framebuffer')
    fb = frames[0]
    integer = lambda name: int.from_bytes((fb / name).read_bytes(), 'big')
    address, size = struct.unpack('>QQ', (fb / 'reg').read_bytes())
    stride = integer('stride')
    if ((fb / 'format').read_bytes() != b'x2r10g10b10\0' or
        integer('width') != width or integer('height') != height or
        stride != width * 4 or size != stride * height or size > 64 * 1024 * 1024):
        raise RuntimeError('Unexpected retained framebuffer layout')
    # Read only the DT-declared framebuffer, after the compositor presents.
    fd = os.open('/dev/mem', os.O_RDONLY | os.O_CLOEXEC)
    try:
        with mmap.mmap(fd, size, flags=mmap.MAP_SHARED, prot=mmap.PROT_READ, offset=address) as mapping:
            raw = mapping[:]
    finally:
        os.close(fd)
    words = array.array('I', raw)
    red = sum(((pixel >> 20) & 1023) > 960 and
              ((pixel >> 10) & 1023) < 64 and (pixel & 1023) < 64 for pixel in words)
    evidence = {'address': hex(address), 'bytes': size, 'format': 'x2r10g10b10',
                'red_pixels': red, 'sha256': hashlib.sha256(raw).hexdigest()}
    (result / 'scanout.json').write_text(json.dumps(evidence, indent=2) + '\n')
    (result / 'scanout.raw').write_bytes(raw)
    if red < width * height // 2:
        raise RuntimeError(f'Panel scanout did not contain the red terminal: {red} pixels')
    return red



def native_kms_status():
    paths = {p.resolve() for p in Path('/sys/kernel/debug/dri').glob('*/m4_status')}
    if len(paths) != 1:
        raise RuntimeError('Expected one native M4 KMS status node')
    path = paths.pop()
    status = dict((k, int(v)) for k, v in (line.split() for line in path.read_text().splitlines()))
    if status['failed'] or not status['active_fb'] or not status['imported'] or not status['completed']:
        raise RuntimeError(f'Native KMS is not scanning out an imported GPU buffer: {status}')
    return path, status


def verify_native_scanout(result, width, height):
    path, status = native_kms_status()
    snapshot = path.with_name('m4_scanout').read_bytes()
    magic, w, h, stride, fmt, fb, imported, size = struct.unpack_from('<8I', snapshot)
    if (magic != 0x4246344d or (w, h) != (width, height) or stride < width * 4 or
        fmt not in (int.from_bytes(b'XR24', 'little'), int.from_bytes(b'AR24', 'little')) or
        imported != 1 or size != stride * height or len(snapshot) != 32 + size):
        raise RuntimeError('Unexpected native scanout snapshot layout')
    words = array.array('I', snapshot[32:])
    red = sum((pixel & 0x00ff0000) > 0x00f00000 and (pixel & 0x0000ff00) < 0x1000 and
              (pixel & 255) < 16 for pixel in words)
    evidence = {**status, 'framebuffer': fb, 'width': w, 'height': h, 'stride': stride,
                'format': fmt, 'imported': True, 'bytes': size, 'red_pixels': red,
                'sha256': hashlib.sha256(snapshot).hexdigest(), 'native_kms': True}
    (result / 'scanout.json').write_text(json.dumps(evidence, indent=2) + '\n')
    (result / 'scanout.m4fb').write_bytes(snapshot)
    if red < width * height // 2:
        raise RuntimeError(f'Native DCP buffer did not contain the red terminal: {red} pixels')
    return red


def main():
    result = Path(os.environ['M4_RENDER_OUTPUT'])
    live_seconds = int(os.environ.get('M4_HYPR_INTERACTIVE_SECONDS', '0'))
    width = int(os.environ.get('M4_HYPR_WIDTH', '256'))
    height = int(os.environ.get('M4_HYPR_HEIGHT', '256'))
    native = os.environ.get('M4_HYPR_NATIVE_KMS') == '1'
    if not (1 <= width <= 2560 and 1 <= height <= (1664 if native else 1600)):
        raise ValueError('Probe size exceeds the J713 panel bounds')
    account = pwd.getpwnam(os.environ['M4_HYPR_USER']) if os.environ.get('M4_HYPR_USER') else None
    runtime = Path(f'/run/user/{account.pw_uid}') if account else Path('/run/m4h')
    if not account:
        runtime.mkdir(mode=0o700, exist_ok=True)
    env = {**os.environ, 'XDG_RUNTIME_DIR': str(runtime),
           'AQ_HEADLESS_RENDER_NODE': '/dev/dri/renderD128',
           'HYPRLAND_NO_SD_NOTIFY': '1', 'AQ_TRACE': '1',
           'GBM_BACKENDS_PATH': os.environ.get('GBM_BACKENDS_PATH', '/root/m4mesa/lib/gbm')}
    user_command = ['runuser', '-u', account.pw_name, '--'] if account else []
    if account:
        env.update({'M4_HYPR_UID': str(account.pw_uid),
                    'DBUS_SESSION_BUS_ADDRESS': f'unix:path={runtime}/bus',
                    'OMARCHY_PATH': '/usr/share/omarchy',
                    'LIBGL_ALWAYS_SOFTWARE': '0', 'QT_OPENGL': 'es2',
                    'QSG_RHI_BACKEND': 'opengl'})
    physical = os.environ.get('M4_HYPR_PHYSICAL') == '1'
    output_name = ('eDP-1' if native else 'Unknown-1') if physical else 'HEADLESS-1'
    env['M4_HYPR_OUTPUT'] = output_name
    if physical:
        env.pop('AQ_HEADLESS_RENDER_NODE', None)
        if native:
            cards = [p for p in Path('/sys/class/drm').glob('card*')
                     if '-' not in p.name and (p / 'device/of_node/compatible').exists()
                     and b'apple,t8132-display-diagnostics\0' in (p / 'device/of_node/compatible').read_bytes()]
            if len(cards) != 1:
                raise RuntimeError('Expected one native M4 KMS device')
            env['AQ_DRM_DEVICES'] = '/dev/dri/' + cards[0].name
        else:
            env['AQ_DRM_DEVICES'] = '/dev/dri/card0'
        env['AQ_USE_RENDER_NODE'] = '1'
    env.pop('WAYLAND_DISPLAY', None)
    env.pop('DISPLAY', None)
    env.pop('HYPRLAND_INSTANCE_SIGNATURE', None)
    config = Path('/opt/m4-hyprland/omarchy.lua') if account else Path(__file__).with_name('minimal.lua')
    command = [*(['start-hyprland', '--'] if account else ['Hyprland', '--i-am-really-stupid']),
               '--config', str(config)]
    if account:
        command = ['python3', str(Path(__file__).with_name('seat-user.py')), account.pw_name, *command]
    if physical:
        command = ['seatd-launch', '--', *command]
    child = None
    terminal = None
    game = None
    with (result / 'hyprland.log').open('w') as log:
        try:
            if physical:
                subprocess.run(['systemctl', 'stop', 'sddm'], check=True, timeout=5)
            if account:
                subprocess.run(['systemctl', 'start', f'user@{account.pw_uid}.service'], check=True, timeout=5)
                if runtime.stat().st_uid != account.pw_uid:
                    raise RuntimeError('Desktop runtime directory has the wrong owner')
            child = subprocess.Popen(command, env=env, stdout=log, stderr=log,
                                     start_new_session=True)
            deadline = time.monotonic() + 7
            instances = []
            while time.monotonic() < deadline:
                if child.poll() is not None:
                    raise RuntimeError(f'Hyprland exited during startup: {child.returncode}')
                instances = list((runtime / 'hypr').glob('*/.socket.sock'))
                sockets = [p for p in runtime.glob('wayland-*') if not p.name.endswith('.lock')]
                if instances and sockets:
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError('Hyprland did not create its sockets within seven seconds')
            env['HYPRLAND_INSTANCE_SIGNATURE'] = instances[-1].parent.name
            env['WAYLAND_DISPLAY'] = sockets[0].name
            monitors = subprocess.check_output(['hyprctl', '-j', 'monitors'], env=env,
                                               text=True, timeout=2)
            (result / 'hyprland-monitors.json').write_text(monitors)
            outputs = json.loads(monitors)
            if not any(m.get('name') == output_name and m.get('width') == width
                       and m.get('height') == height for m in outputs):
                raise RuntimeError(f'Expected active {width}x{height} {output_name} output')
            terminal = subprocess.Popen([*user_command, 'foot', '--app-id=m4-gpu-check',
                '-o', 'colors.background=ff0000', '-o', 'font=monospace:size=10',
                '/bin/sh', '-c', 'printf "M4 GPU compositing\n"; ' + ('exec /bin/bash' if live_seconds else 'sleep 8')],
                env=env, stdout=log, stderr=log, start_new_session=True,
                cwd=account.pw_dir if account else '/')
            # A Wayland socket is ready before foot has mapped its first
            # buffer. Wait for that window before requesting a screencopy;
            # a fixed startup delay can capture the empty desktop.
            deadline = time.monotonic() + 7
            while time.monotonic() < deadline:
                if terminal.poll() is not None:
                    raise RuntimeError('The validation terminal exited before mapping')
                clients = json.loads(subprocess.check_output(
                    ['hyprctl', '-j', 'clients'], env=env, text=True, timeout=2))
                if any(c.get('class') == 'm4-gpu-check' and c.get('mapped')
                       for c in clients):
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError('The validation terminal did not map within seven seconds')
            time.sleep(0.3)
            pixels = result / 'hyprland.ppm'
            subprocess.run(['grim', '-t', 'ppm', '-o', output_name, str(pixels)],
                           env=env, check=True, stdout=log, stderr=log, timeout=6)
            data = pixels.read_bytes()
            header, dimensions, maximum, rgb = data.split(b'\n', 3)
            image_width, image_height = map(int, dimensions.split())
            if (header, image_width, image_height, maximum) != (b'P6', width, height, b'255') or len(rgb) != width * height * 3:
                raise RuntimeError('Unexpected screenshot format')
            red = sum(rgb[i] > 240 and rgb[i+1] < 16 and rgb[i+2] < 16
                      for i in range(0, len(rgb), 3))
            if red < width * height // 2:
                raise RuntimeError(f'Expected a composed red terminal, found {red} red pixels')
            log.flush()
            text = (result / 'hyprland.log').read_text()
            if not any('Renderer:' in line and 'zink' in line.lower() and 'M4' in line
                       for line in text.splitlines()):
                raise RuntimeError('Hyprland did not identify the M4 Zink renderer')
            scanout_red = (verify_native_scanout(result, width, height) if native else
                           verify_scanout(result, width, height)) if physical else None
            summary = {'passed': True, 'hyprland': True,
                       'scope': f'{width}x{height} {output_name} compositor',
                       'physical_display': physical, 'native_kms': native,
                       'scanout_red_pixels_verified': scanout_red,
                       'red_pixels_verified': red, 'screenshot': pixels.name}
            if os.environ.get('M4_TEST_SUPERTUXKART') == '1':
                os.killpg(terminal.pid, signal.SIGTERM)
                terminal.wait(timeout=4)
                game_env = {**env, 'IRR_DEVICE_TYPE':'wayland',
                            **({'AGX_MESA_DEBUG':'shaders,internal', 'HK_G17P_LAUNCH_TRACE':'1'} if os.getenv('M4_STK_SHADER_TRACE') == '1' else {}),
                            'XDG_CONFIG_HOME':str(Path(account.pw_dir)/'.config/m4-asahi-supertuxkart')}
                with (result/'supertuxkart.log').open('w') as game_log:
                    game = subprocess.Popen([*user_command, '/usr/bin/supertuxkart',
                        '--render-driver=opengl', '--windowed', '--screensize=800x450',
                        '--race-now', '--track=lighthouse', '--numkarts=4',
                        '--laps=1', '--profile-laps=1', '--log=0',
                        '--disable-texture-compression', '--disable-dynamic-lights'],
                        env=game_env,stdout=game_log,stderr=game_log,
                        start_new_session=True,cwd=account.pw_dir)
                    deadline=time.monotonic()+15
                    while time.monotonic()<deadline:
                        if game.poll() is not None:
                            raise RuntimeError(f'SuperTuxKart exited at startup: {game.returncode}')
                        clients=subprocess.check_output(['hyprctl','-j','clients'],env=env,text=True,timeout=2)
                        if any('supertuxkart' in c.get('class','').lower() and c.get('mapped') for c in json.loads(clients)):
                            break
                        time.sleep(0.2)
                    else:
                        raise RuntimeError('SuperTuxKart did not map within fifteen seconds')
                    (result/'supertuxkart-clients.json').write_text(clients)
                    subprocess.run(['hyprctl','dispatch','hl.dsp.window.float({action="toggle"})'],env=env,check=True,timeout=2)
                    subprocess.run(['hyprctl','dispatch','hl.dsp.window.resize({x=800,y=450})'],env=env,check=True,timeout=2)
                    subprocess.run(['hyprctl','dispatch','hl.dsp.window.center()'],env=env,check=True,timeout=2)
                    time.sleep(5)
                    subprocess.run(['grim','-t','ppm','-o',output_name,str(result/'supertuxkart.ppm')],env=env,check=True,timeout=120 if live_seconds else 6)
                    if game.poll() is not None:
                        raise RuntimeError(f'SuperTuxKart exited during first frames: {game.returncode}')
                    game_log.flush()
                    text=(result/'supertuxkart.log').read_text()
                    if not ('zink' in text.lower() and 'M4' in text):
                        raise RuntimeError('SuperTuxKart did not identify the M4 Zink renderer')
                    summary.update(supertuxkart_started=True)
            if os.environ.get('M4_TEST_SUPERTUX') == '1':
                os.killpg(terminal.pid, signal.SIGTERM)
                terminal.wait(timeout=4)
                game_env = {**env, 'SDL_VIDEODRIVER': 'wayland',
                            'MESA_GL_VERSION_OVERRIDE': '3.3',
                            'LD_PRELOAD': '/opt/asahi-m4-port/lib/gl-renderer-audit.so'}
                with (result / 'supertux.log').open('w') as game_log:
                    game = subprocess.Popen([*user_command, '/usr/bin/supertux2',
                        '--renderer', 'opengl', '--window', '--geometry', '800x450',
                        '--disable-sound', '--disable-music', '--show-fps', '--verbose',
                        '/usr/share/games/supertux2/levels/world1/welcome_antarctica.stl'],
                        env=game_env, stdout=game_log, stderr=game_log,
                        start_new_session=True, cwd=account.pw_dir if account else '/')
                    deadline = time.monotonic() + 12
                    while time.monotonic() < deadline:
                        if game.poll() is not None:
                            raise RuntimeError(f'SuperTux exited: {game.returncode}')
                        clients = subprocess.check_output(['hyprctl','-j','clients'],env=env,text=True,timeout=2)
                        if any('supertux' in c.get('class','').lower() and c.get('mapped') for c in json.loads(clients)):
                            break
                        time.sleep(0.2)
                    else:
                        raise RuntimeError('SuperTux did not map within twelve seconds')
                    (result/'supertux-clients.json').write_text(clients)
                    game_pids = subprocess.check_output(['pgrep','-u','1000','-x','supertux2'],text=True).split()
                    assert len(game_pids) == 1, game_pids
                    (result/'supertux-maps.txt').write_text(Path('/proc',game_pids[0],'maps').read_text())
                    subprocess.run(['hyprctl','dispatch','hl.dsp.window.float({action="toggle"})'],env=env,check=True,timeout=2)
                    subprocess.run(['hyprctl','dispatch','hl.dsp.window.resize({x=800,y=450})'],env=env,check=True,timeout=2)
                    subprocess.run(['hyprctl','dispatch','hl.dsp.window.center()'],env=env,check=True,timeout=2)
                    # LevelIntro waits for input. Target the actual game after
                    # setup, and hold across a frame rather than sending an
                    # early press/release before its controller is ready.
                    time.sleep(3)
                    for key_state in ('down', 'up'):
                        dispatch = 'hl.dsp.send_key_state({mods="",key="space",state="' + key_state + '",window="pid:' + game_pids[0] + '"})'
                        subprocess.run(['hyprctl','dispatch',dispatch],env=env,check=True,timeout=2)
                        if key_state == 'down':
                            time.sleep(1)
                    time.sleep(3)
                    for frame in range(2):
                        subprocess.run(['grim','-t','ppm','-o',output_name,str(result/f'supertux-{frame}.ppm')],env=env,check=True,timeout=6)
                        if game.poll() is not None:
                            raise RuntimeError('SuperTux stopped before screenshot')
                        if frame == 0:
                            time.sleep(1)
                    game_log.flush()
                    text = (result/'supertux.log').read_text()
                    if not ('zink' in text.lower() and 'M4' in text):
                        raise RuntimeError('SuperTux did not identify the M4 Zink renderer')
                    path,status = native_kms_status()
                    (result/'supertux-scanout.m4fb').write_bytes(path.with_name('m4_scanout').read_bytes())
                    layers = subprocess.check_output(['hyprctl','-j','layers'],env=env,text=True,timeout=2)
                    (result/'omarchy-layers.json').write_text(layers)
                    assert 'omarchy-bar' in layers and 'omarchy-background' in layers
                    summary.update(omarchy_panel_and_background=True, supertux=True, supertux_renderer='M4 Zink/Honeykrisp', native_kms_status=status)
                (result/'desktop-result.json').write_text(json.dumps(summary,indent=2)+'\n')
            if live_seconds:
                stop_file = runtime / 'm4-hyprland-stop' if account else Path('/run/m4-hyprland-stop')
                stop_file.unlink(missing_ok=True)
                if account:
                    # The red surface has served its validation purpose.
                    # Close it so Omarchy's actual shell and wallpaper show.
                    if terminal.poll() is None:
                        os.killpg(terminal.pid, signal.SIGTERM)
                        terminal.wait(timeout=4)
                    layers = subprocess.check_output(['hyprctl', '-j', 'layers'],
                                                     env=env, text=True, timeout=2)
                    (result / 'omarchy-layers.json').write_text(layers)
                    if 'omarchy-bar' not in layers or 'omarchy-background' not in layers:
                        raise RuntimeError('Omarchy panel and background did not appear')
                    summary.update({'desktop_user': account.pw_name,
                                    'quickshell_backend': 'software',
                                    'omarchy_panel_and_background': True})
                print(json.dumps({'event': 'ready', 'result': summary}), flush=True)
                deadline = time.monotonic() + live_seconds
                checks = 0
                while time.monotonic() < deadline and not stop_file.exists() and not Path('/run/m4-hyprland-stop').exists():
                    if summary.get('supertuxkart_started'):
                        game_text=(result/'supertuxkart.log').read_text()
                        if any(x in game_text for x in ('MESA: error:', 'no grounded AGX3 encoding', 'Assertion `')):
                            raise RuntimeError('SuperTuxKart driver error; see shader log')
                        if game.poll() is not None:
                            match=re.search(r'Number of frames: (\d+) time ([0-9.]+), Average FPS: ([0-9.]+)',game_text)
                            if game.returncode != 0 or not match or int(match[1]) < 1:
                                raise RuntimeError(f'SuperTuxKart exited without a completed profile race: {game.returncode}')
                            summary['supertuxkart_demo']={'completed':True,'track':'lighthouse','karts':4,'laps':1,
                                'frames':int(match[1]),'seconds':float(match[2]),'average_fps':float(match[3]),'exit_status':0}
                            (result/'supertuxkart-demo.json').write_text(json.dumps(summary['supertuxkart_demo'],indent=2)+'\n')
                            break
                    with (result / 'health-shaders.stderr').open('a') as diagnostics:
                        check = subprocess.run(['/root/m4-vulkan-compute-min'],
                                               env={**env, **({'HK_G17P_LAUNCH_TRACE': '1'}
                                                    if os.getenv('M4_HYPR_TRACE_HEALTH') == '1' else {})},
                                               stdout=subprocess.PIPE, stderr=diagnostics,
                                               text=True, timeout=120 if summary.get('supertuxkart_started') else 8, check=True)
                    assert 'PASS — 32 buffer writes' in check.stdout, check.stdout
                    evidence = {'passed':True, 'vulkan':True, 'values_verified':32, 'device_id':0x8132, 'sequence':checks, 'checked_at':time.monotonic()}
                    reply = json.loads(subprocess.check_output(['hyprctl', '-j', 'monitors'],
                                                               env=env, text=True, timeout=2))
                    evidence['compositor_responsive'] = any(m.get('name') == output_name and
                        not m.get('disabled', False) for m in reply)
                    if native:
                        _, evidence['native_kms_status'] = native_kms_status()
                    # Parent validates all fields before feeding the hardware watchdog.
                    print(json.dumps({'event': 'gpu-progress', 'check': evidence}), flush=True)
                    if checks in (2,4,8):
                        subprocess.run(['grim','-t','ppm','-o',output_name,str(result/f'live-{checks}.ppm')],env=env,check=True,timeout=120 if summary.get('supertuxkart_started') else 6)
                    checks += 1
                    time.sleep(min(5, max(0, deadline - time.monotonic())))
                if summary.get('supertuxkart_started') and not summary.get('supertuxkart_demo',{}).get('completed'):
                    raise RuntimeError('SuperTuxKart demo did not finish before the test deadline or stop request')
                summary['interactive_seconds'] = live_seconds
                summary['health_checks'] = checks
            print(json.dumps(summary), flush=True)
        finally:
            for process in (game, terminal, child):
                if process is None:
                    continue
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                # seatd-launch can exit before Hyprland and Xwayland. Wait
                # for the group, not just the launcher, then reap descendants.
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    process.poll()
                    try:
                        os.killpg(process.pid, 0)
                    except ProcessLookupError:
                        break
                    time.sleep(0.05)
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=2)
            if physical:
                if account:
                    # Omarchy starts user services outside the compositor's
                    # process group. Stop them and discard its imported private
                    # graphics environment before restoring ordinary login.
                    subprocess.run(['systemctl', 'stop', f'user@{account.pw_uid}.service'], check=True, timeout=8)
                # DCP remains pinned until the next guarded reboot.
                pass


if __name__ == '__main__':
    main()
