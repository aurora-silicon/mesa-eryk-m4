#!/usr/bin/env python3
"""Private 1000 Hz Weston headless display for M3 CTS WSI cases."""
from contextlib import contextmanager
import json
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

@contextmanager
def headless(out, env):
    runtime=Path('/run')/f'm3-cts-headless-{os.getpid()}'
    runtime.mkdir(mode=0o700)
    # CTS prefixes may contain only the Vulkan ICD. EGL, GBM and Gallium must
    # come from one complete compositor installation; inheriting GBM from the
    # desktop while overriding LD_LIBRARY_PATH silently disabled Xwayland DRI3.
    prefix=Path('/opt/asahi-m3m4-e6a74082f1d')
    display=dict(env,XDG_RUNTIME_DIR=str(runtime),WAYLAND_DISPLAY='cts-wayland',
                 LD_LIBRARY_PATH=':'.join(filter(None,[env.get('LD_LIBRARY_PATH'),str(prefix/'lib')])),
                 GBM_BACKENDS_PATH=str(prefix/'lib/gbm'),
                 LIBGL_DRIVERS_PATH=str(prefix/'lib/dri'),
                 __EGL_VENDOR_LIBRARY_FILENAMES=str(prefix/'egl.json'),
                 GALLIUM_DRIVER='zink',MESA_LOADER_DRIVER_OVERRIDE='zink')
    proc=None
    layout=None
    layout_log=None
    try:
        with (out/'headless.log').open('w') as log:
            proc=subprocess.Popen(['weston','--backend=headless','--renderer=gl','--refresh-rate=1000000','--width=1024','--height=1024','--socket=cts-wayland','--xwayland','--no-config','--idle-time=0'],env=display,stdout=log,stderr=subprocess.STDOUT)
            deadline=time.monotonic()+5
            while True:
                assert proc.poll() is None,'Headless Weston exited'
                text=(out/'headless.log').read_text(errors='replace')
                match=re.search(r'xserver listening on display (:\d+)',text,re.I)
                if (runtime/'cts-wayland').exists() and match:
                    display['DISPLAY']=match.group(1)
                    break
                assert time.monotonic()<deadline,'Headless display startup timed out'
                time.sleep(.1)
            # Connect once to start lazy Xwayland and verify presentation before
            # attributing any test failure to the Vulkan driver.
            info=subprocess.run(['xdpyinfo','-queryExtensions'],env=display,
                                text=True,capture_output=True,timeout=5)
            (out/'headless-x11-info.txt').write_text(info.stdout+info.stderr)
            assert info.returncode==0, 'Headless Xwayland connection failed'
            assert re.search(r'^\s+DRI3\s',info.stdout,re.M), 'Headless Xwayland lacks DRI3'
            assert re.search(r'^\s+Present\s',info.stdout,re.M), 'Headless Xwayland lacks Present'
            files=[prefix/'egl.json',prefix/'lib/libEGL_mesa.so.0',
                   prefix/'lib/libgbm.so.1',prefix/'lib/gbm/dri_gbm.so',
                   prefix/'lib/libgallium-26.1.4.so',Path(shutil.which('weston')),
                   Path(shutil.which('Xwayland'))]
            (out/'headless-binary-hashes.json').write_text(json.dumps({str(p):hashlib.file_digest(p.open('rb'),'sha256').hexdigest() for p in files},indent=2)+'\n')
            (out/'headless-environment.json').write_text(json.dumps({k:v for k,v in display.items() if k.startswith(('XDG_','WAYLAND','DISPLAY','VK_','LD_','GALLIUM','MESA_','GBM_','LIBGL_','__EGL_'))},indent=2)+'\n')
            if display.get('M3_CTS_TILE_X11') == '1':
                layout_log=(out/'x11-layout.jsonl').open('w')
                layout=subprocess.Popen(['python3',str(Path(__file__).with_name('cts_x11_layout.py'))],env=display,stdout=layout_log,stderr=subprocess.STDOUT)
            yield display
    finally:
        if layout is not None:
            layout.terminate()
            try:layout.wait(timeout=2)
            except subprocess.TimeoutExpired:layout.kill();layout.wait(timeout=2)
        if layout_log is not None:layout_log.close()
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:proc.wait(timeout=2)
            except subprocess.TimeoutExpired:proc.kill();proc.wait(timeout=2)
        shutil.rmtree(runtime)
