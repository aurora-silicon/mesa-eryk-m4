#!/usr/bin/env python3
"""Target-side GL probe environment. Run ONLY inside the mandatory guard."""
import os
from pathlib import Path
import subprocess
import sys
import time

if len(sys.argv) < 2:
    raise SystemExit("usage: run-m4-gl.py PROGRAM [ARGS...] (inside watchdog/thermal guard)")
if Path('/sys/module/g16_lab').exists():
    raise SystemExit("Lab module loaded; refusing Asahi GL probe")
subprocess.run(['modprobe', 'asahi'], check=True, timeout=20)
env = dict(os.environ)
env.update({
    'VK_DRIVER_FILES': '/opt/asahi-m4-port/asahi_icd.json',
    'ASAHI_M4_EXPERIMENTAL': '1', 'MESA_SHADER_CACHE_DISABLE': 'true',
    'LD_LIBRARY_PATH': '/opt/asahi-m4-port/lib',
    'LIBGL_DRIVERS_PATH': '/opt/asahi-m4-port/lib/dri',
    'GBM_BACKENDS_PATH': '/opt/asahi-m4-port/lib/gbm',
    '__EGL_VENDOR_LIBRARY_FILENAMES': '/opt/asahi-m4-port/egl.json',
    'MESA_LOADER_DRIVER_OVERRIDE': 'zink', 'GALLIUM_DRIVER': 'zink',
    'LIBGL_ALWAYS_SOFTWARE': '0', 'MESA_GL_VERSION_OVERRIDE': '3.3',
    'MESA_GLES_VERSION_OVERRIDE': '3.0',
})
env.setdefault('ASAHI_MESA_DEBUG', 'nocompress')
env.setdefault('ZINK_DEBUG', 'sync')
env.setdefault('AGX_MESA_DEBUG', 'shaders,internal')
env.setdefault('HK_G17P_LAUNCH_TRACE', '1')
print('Probe environment:', {k: v for k, v in sorted(env.items()) if
      k.startswith(('M4_TEST_', 'ASAHI_', 'AGX_', 'HK_', 'ZINK_'))}, flush=True)
def marker(phase):
    message = f"M4 probe {phase} monotonic={time.monotonic():.9f}"
    with open('/dev/kmsg', 'w') as log:
        log.write(message + '\n')
    print(message, flush=True)

capture = env.get('M4_FIRMWARE_CAPTURE_DIR')
def snapshot(label):
    if capture:
        subprocess.run([sys.executable, str(Path(__file__).with_name('firmware-snapshot.py')),
                        '--guard-output', capture, '--label', label], check=True, timeout=5)

snapshot('before-workload')
marker('start')
try:
    result = subprocess.run(sys.argv[1:], env=env, timeout=30).returncode
finally:
    marker('end')
    snapshot('after-workload')
    if capture:
        subprocess.run([sys.executable, str(Path(__file__).with_name('g16-fault-dump.py')),
                        'collect', '--guard-output', capture, '--allow-missing'], check=True, timeout=5)
sys.exit(result)
