#!/usr/bin/env python3
"""Guest-only Steam session: keep FEX IPC reachable across CEF namespaces.

Run as the desktop user inside muvm, before the first emulated process.
The private socket lives in shared home, which pressure-vessel preserves.
Native dbus-run-session supplies a bus for this VM without borrowing the
desktop's bus or changing Chromium's sandbox/rendering options.
"""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile

assert os.getuid() != 0, 'Steam must run as the desktop user'
assert os.sysconf('SC_PAGE_SIZE') == 4096 and Path('/run/fex-emu/rootfs').is_dir(), 'invoke inside the configured microVM'
assert len(sys.argv) > 1
base = Path.home() / '.cache/m4-steam'
base.mkdir(mode=0o700, parents=True, exist_ok=True)
child = None
def interrupt(signum, frame):
    raise SystemExit(128 + signum)
signal.signal(signal.SIGTERM, interrupt)
signal.signal(signal.SIGINT, interrupt)
with tempfile.TemporaryDirectory(prefix='vm-', dir=base) as runtime:
    socket = str(Path(runtime) / 'fex.sock')
    assert len(os.fsencode(socket)) < 108
    env = dict(os.environ, FEX_SERVERSOCKETPATH=socket)
    # D-Bus activates native ARM services. Keep their driver paths native,
    # then restore the x86 graphics environment only for the FEX command.
    native_graphics = {
        'VK_DRIVER_FILES': '/opt/asahi-m4-port/asahi_icd.json',
        'LD_LIBRARY_PATH': '/opt/asahi-m4-port/lib',
        'LIBGL_DRIVERS_PATH': '/opt/asahi-m4-port/lib/dri',
        'GBM_BACKENDS_PATH': '/opt/asahi-m4-port/lib/gbm',
        '__EGL_VENDOR_LIBRARY_FILENAMES': '/opt/asahi-m4-port/egl.json',
    }
    guest_command = ['/usr/bin/env', *(f'{key}={env[key]}' for key in native_graphics
                                     if key in env), *sys.argv[1:]]
    env.update(native_graphics)
    try:
        child = subprocess.Popen(['/usr/bin/dbus-run-session', '--', *guest_command],
                                 env=env, start_new_session=True)
        raise SystemExit(child.wait())
    finally:
        if child is not None and child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()
