#!/usr/bin/env python3
"""Bounded Firefox graphics/WebGL probe inside the existing GPU experiment guard.

Uses Firefox's local Marionette protocol for text diagnostics, not screenshot
automation. The isolated test profile enables local automation only for this run.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import pwd
import signal
import socket
import stat
import subprocess
import time


class Marionette:
    def __init__(self):
        self.socket = socket.create_connection(('127.0.0.1', 2828), timeout=2)
        self.socket.settimeout(8)
        self.sequence = 0
        self.read()

    def read(self):
        length = bytearray()
        while True:
            byte = self.socket.recv(1)
            if not byte:
                raise EOFError('Marionette disconnected')
            if byte == b':':
                break
            length += byte
        remaining = int(length)
        result = bytearray()
        while remaining:
            data = self.socket.recv(remaining)
            if not data:
                raise EOFError('Marionette disconnected')
            result += data
            remaining -= len(data)
        return json.loads(result)

    def call(self, name, args=None):
        self.sequence += 1
        data = json.dumps([0, self.sequence, name, args or {}]).encode()
        self.socket.sendall(str(len(data)).encode() + b':' + data)
        response = self.read()
        assert response[:2] == [1, self.sequence], response
        if response[2]:
            raise RuntimeError(response[2])
        result = response[3]
        return result.get('value', result) if isinstance(result, dict) else result

    def script(self, script, asynchronous=False):
        return self.call('WebDriver:ExecuteAsyncScript' if asynchronous else 'WebDriver:ExecuteScript',
                         dict(script=script, args=[], newSandbox=True, sandbox='default', line=1, filename='m4-firefox-probe'))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--guard-output', required=True, type=Path)
    p.add_argument('--url', default='https://mdn.github.io/dom-examples/webgl-examples/tutorial/sample6/')
    p.add_argument('--observe-seconds', type=int, default=6, choices=range(1, 21))
    p.add_argument('--force-gpu', action='store_true')
    p.add_argument('--renderer', choices=['asahi', 'lavapipe'], default='asahi')
    p.add_argument('--capture', action='store_true')
    p.add_argument('--capture-profile', type=Path)
    p.add_argument('--readback-trace', type=Path)
    p.add_argument('--freeze-animation', action='store_true')
    p.add_argument('--page-script', type=Path, help='Retained read-only page diagnostics after observation')
    p.add_argument('--seed-profile', type=Path, help='Copy a closed existing profile into the isolated test profile')
    a = p.parse_args()
    spec = importlib.util.spec_from_file_location('fw', Path(__file__).with_name('firmware-snapshot.py'))
    fw = importlib.util.module_from_spec(spec); spec.loader.exec_module(fw)
    boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
    def check():
        fw.check_guard(a.guard_output, boot)
    check()
    out = a.guard_output / 'firefox'
    out.mkdir(exist_ok=False)
    user = pwd.getpwnam('e')
    profile = Path(user.pw_dir) / '.cache/m4-firefox-probes' / a.guard_output.name
    if a.seed_profile:
        import shutil
        assert a.seed_profile.is_dir() and not (a.seed_profile / "lock").is_symlink(), "Close source Firefox first"
        shutil.copytree(a.seed_profile, profile, ignore=shutil.ignore_patterns("lock", ".parentlock", "parent.lock", "cache2", "shader-cache", "startupCache"))
        for entry in profile.rglob("*"):
            os.chown(entry, user.pw_uid, user.pw_gid)
    else:
        profile.mkdir(parents=True, exist_ok=False)
    for directory in (profile.parent, profile):
        os.chown(directory, user.pw_uid, user.pw_gid)
    prefs = {'marionette.port': 2828, 'remote.prefs.recommended': False,
             'browser.shell.checkDefaultBrowser': False, 'browser.startup.page': 0,
             'browser.startup.homepage': 'about:blank', 'browser.aboutwelcome.enabled': False,
             'browser.startup.homepage_override.mstone': 'ignore', 'datareporting.policy.dataSubmissionPolicyBypassNotification': True,
             'termsofuse.bypassNotification': True, 'browser.tabs.warnOnClose': False}
    if a.force_gpu:
        prefs.update({'gfx.webrender.all': True, 'layers.acceleration.force-enabled': True,
                      'webgl.force-enabled': True})
    settings = '\n'.join(f'user_pref({json.dumps(k)}, {json.dumps(v)});' for k, v in prefs.items()) + '\n'
    (profile / 'user.js').write_text(settings)
    os.chown(profile / 'user.js', user.pw_uid, user.pw_gid)
    (out / 'user.js').write_text(settings)
    capture = profile / 'compiler-capture'
    capture.mkdir()
    os.chown(capture, user.pw_uid, user.pw_gid)
    env = dict(os.environ)
    env.update(HOME=user.pw_dir, USER='e', LOGNAME='e', MOZ_ENABLE_WAYLAND='1',
               XDG_RUNTIME_DIR=f'/run/user/{user.pw_uid}',
               DBUS_SESSION_BUS_ADDRESS=f'unix:path=/run/user/{user.pw_uid}/bus',
               AGX_COMPILER_CAPTURE_DIR=str(capture),
               MESA_SHADER_CACHE_DIR=str(profile / 'mesa-cache'))
    sockets = [f for f in Path(env['XDG_RUNTIME_DIR']).glob('wayland-*') if stat.S_ISSOCK(f.stat().st_mode)]
    assert len(sockets) == 1, sockets
    env['WAYLAND_DISPLAY'] = sockets[0].name
    for line in Path('/etc/environment.d/90-omarchy-j713-rendering.conf').read_text().splitlines():
        if line and not line.startswith('#') and '=' in line:
            key, value = line.split('=', 1)
            env[key] = value
    # A caller may select a qualified software reference/capture environment.
    for key, value in os.environ.items():
        if key.startswith(('GFXRECON_', 'VK_', 'LIBGL_ALWAYS_SOFTWARE')):
            env[key] = value
    env['VK_DRIVER_FILES'] = '/opt/asahi-m4-port/asahi_icd.json' if a.renderer == 'asahi' else '/usr/share/vulkan/icd.d/lvp_icd.json'
    env['LIBGL_ALWAYS_SOFTWARE'] = '0' if a.renderer == 'asahi' else '1'
    traces = profile / 'vulkan-capture'
    traces.mkdir(); os.chown(traces, user.pw_uid, user.pw_gid)
    if a.capture:
        env.update(VK_LAYER_PATH='/opt/gfxreconstruct/share/vulkan/explicit_layer.d:/usr/share/vulkan/explicit_layer.d',
                   VK_INSTANCE_LAYERS='VK_LAYER_LUNARG_gfxreconstruct',
                   GFXRECON_CAPTURE_FILE=str(traces / 'firefox-${ProcessId}.gfxr'),
                   GFXRECON_CAPTURE_FILE_TIMESTAMP='false', GFXRECON_CAPTURE_FILE_FLUSH='true')
        if a.capture_profile:
            import shutil
            shutil.copyfile(a.capture_profile, profile / 'capture-profile.json')
            report_path = a.capture_profile.with_suffix('.report.json')
            report = json.loads(report_path.read_text())
            shutil.copyfile(report_path, out / 'capture-profile.report.json')
            env.update(VK_INSTANCE_LAYERS=env['VK_INSTANCE_LAYERS']+':VK_LAYER_KHRONOS_profiles',
                       VK_KHRONOS_PROFILES_PROFILE_FILE=str(profile/'capture-profile.json'),
                       VK_KHRONOS_PROFILES_SIMULATE_CAPABILITIES='SIMULATE_API_VERSION_BIT,SIMULATE_FEATURES_BIT,SIMULATE_EXTENSIONS_BIT,SIMULATE_PROPERTIES_BIT,SIMULATE_FORMATS_BIT',
                       VK_KHRONOS_PROFILES_EXCLUDE_DEVICE_EXTENSIONS=','.join(report['removed_reference_extensions']),
                       VK_KHRONOS_PROFILES_DEFAULT_FEATURE_VALUES='DEFAULT_FEATURE_VALUES_FALSE')
    readback = profile / 'gl-readback'
    readback.mkdir(); os.chown(readback, user.pw_uid, user.pw_gid)
    if a.readback_trace:
        import hashlib
        tracer = a.readback_trace.resolve(strict=True)
        env.update(LD_PRELOAD=str(tracer), M4_GL_READBACK_TRACE_DIR=str(readback))
        (out/'readback-tracer.json').write_text(json.dumps(dict(path=str(tracer),
            sha256=hashlib.sha256(tracer.read_bytes()).hexdigest()))+'\n')
    command = ['runuser', '-u', 'e', '--', '/usr/bin/firefox', '--no-remote', '--new-instance',
               '--marionette', '--remote-allow-system-access', '--profile', str(profile), 'about:blank']
    result = dict(command=command, url=a.url, profile=str(profile), boot_id=boot,
                  started=time.monotonic(), force_gpu=a.force_gpu,
                  environment={k:v for k,v in env.items() if k.startswith(('VK_', 'MESA_', 'GALLIUM_', 'LIBGL_', 'LD_LIBRARY', 'MOZ_', 'GFXRECON_'))})
    (out / 'start.json').write_text(json.dumps(result, indent=2)+'\n')
    child = None
    remote = None
    def interrupted(signum, frame):
        raise InterruptedError(f'Firefox interrupted by signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        with (out / 'browser.log').open('w') as log:
            child = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            deadline = time.monotonic()+12
            while True:
                check()
                if child.poll() is not None:
                    raise RuntimeError(f'Firefox exited: {child.returncode}')
                try:
                    remote = Marionette()
                    break
                except ConnectionRefusedError:
                    if time.monotonic() > deadline:
                        raise TimeoutError('Firefox automation did not start')
                    time.sleep(.2)
            remote.call('WebDriver:NewSession', {'capabilities': {'alwaysMatch': {'acceptInsecureCerts': False}}})
            remote.call('WebDriver:SetTimeouts', {'script': 6000, 'pageLoad': 12000, 'implicit': 0})
            remote.call('Marionette:SetContext', {'value': 'chrome'})
            gfx = remote.script('const done=arguments[arguments.length-1]; ChromeUtils.importESModule("resource://gre/modules/Troubleshoot.sys.mjs").Troubleshoot.snapshot().then(s=>done(s.graphics));', True)
            (out / 'graphics.json').write_text(json.dumps(gfx, indent=2)+'\n')
            print('Firefox graphics:', json.dumps({k:gfx.get(k) for k in ('windowLayerManagerType','adapterDescription','numAcceleratedWindows','numTotalWindows')}), flush=True)
            remote.call('Marionette:SetContext', {'value': 'content'})
            remote.call('WebDriver:Navigate', {'url': a.url})
            until = time.monotonic()+a.observe_seconds
            while time.monotonic() < until:
                check(); time.sleep(.25)
            if a.freeze_animation:
                remote.script('window.requestAnimationFrame = () => 0; return true;')
                check(); time.sleep(.25)
            # Do not call getContext here: that can create a new context after
            # failed website startup and incorrectly suggest a working page.
            page = remote.script('return {url:location.href,title:document.title,ready:document.readyState,probe:JSON.parse(document.documentElement.dataset.m4Probe||"null"),canvases:Array.from(document.querySelectorAll("canvas")).map(c=>({width:c.width,height:c.height,clientWidth:c.clientWidth,clientHeight:c.clientHeight}))};')
            if a.page_script:
                script = a.page_script.read_text()
                (out / 'page-diagnostics.js').write_text(script)
                page['diagnostics'] = remote.script(script)
                diagnostics = page['diagnostics']
                if isinstance(diagnostics, dict) and 'pixelsBase64' in diagnostics:
                    import base64
                    pixels = base64.b64decode(diagnostics.pop('pixelsBase64'), validate=True)
                    (out / 'pixels.rgba').write_bytes(pixels)
                    diagnostics['pixels_file'] = 'pixels.rgba'
                    diagnostics['pixels_bytes'] = len(pixels)
            result['page'] = page
            print('Firefox page:', json.dumps(page), flush=True)
            for proc in Path('/proc').glob('[0-9]*'):
                try:
                    if proc.stat().st_uid == user.pw_uid and 'firefox' in (proc/'exe').resolve().name:
                        (out / f'{proc.name}-maps.txt').write_text((proc/'maps').read_text())
                except (FileNotFoundError, ProcessLookupError):
                    pass
            check()
            # Preserve the page even when DPMS disables the hardware scanout.
            # This is separate from the compositor screenshot below.
            import base64
            shot = remote.call('WebDriver:TakeScreenshot', {})
            (out / 'page.png').write_bytes(base64.b64decode(shot))
            nodes = {f.resolve() for f in Path('/sys/kernel/debug/dri').glob('*/m4_scanout')}
            if len(nodes) == 1:
                try:
                    (out / 'desktop.m4fb').write_bytes(nodes.pop().read_bytes())
                except OSError as error:
                    import errno
                    if error.errno != errno.ENODATA:
                        raise
                    result['scanout_unavailable'] = str(error)
            check()
            screenshot = subprocess.run(['runuser', '-u', 'e', '--', 'grim', '-'],
                                        env=env, capture_output=True, timeout=5)
            if screenshot.returncode == 0:
                (out / 'compositor.png').write_bytes(screenshot.stdout)
            else:
                result['compositor_screenshot_error'] = screenshot.stderr.decode(errors='replace')
            result['completed'] = True
    except BaseException as error:
        result['error'] = repr(error)
        raise
    finally:
        if remote:
            remote.socket.close()
        if child and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL); child.wait(timeout=3)
        result['ended'] = time.monotonic()
        import shutil
        shutil.copytree(readback, out / 'gl-readback')
        shutil.copytree(capture, out / 'compiler-capture')
        shutil.copytree(traces, out / 'vulkan-capture')
        (out / 'result.json').write_text(json.dumps(result, indent=2)+'\n')
        print((out / 'browser.log').read_text(errors='replace')[-4000:], flush=True)
        os.sync()
    print('PASS: Firefox probe completed; check graphics and WebGL evidence', flush=True)


if __name__ == '__main__':
    main()
