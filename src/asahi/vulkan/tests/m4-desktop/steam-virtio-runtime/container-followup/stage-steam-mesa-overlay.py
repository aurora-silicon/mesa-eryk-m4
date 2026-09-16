#!/usr/bin/env python3
"""Stage an x86_64 M4 Mesa FEX overlay; does not install or run GPU work.

Build Zink, Honeykrisp, EGL/GLX and GBM first (draw-use-llvm=false avoids an
unnecessary runtime LLVM dependency). Pack the resulting directory with
mkfs.erofs -b4096 and append it after the distribution's FEX rootfs/Mesa
overlays. The guest has 4 KiB pages even when packing on a 16 KiB host.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--arch', choices=('x86_64', 'i686'), default='x86_64')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
libname = 'lib32' if a.arch == 'i686' else 'lib'
elfclass, machine = (1, 3) if a.arch == 'i686' else (2, 62)
lib = a.output / 'opt/asahi-m4-port' / libname
lib.mkdir(parents=True)
sources = {
    'libvulkan_asahi.so': 'src/asahi/vulkan/libvulkan_asahi.so',
    'libgallium-26.1.4.so': 'src/gallium/targets/dri/libgallium-26.1.4.so',
    'libEGL_mesa.so.0': 'src/egl/libEGL_mesa.so.0.0.0',
    'libGLX_mesa.so.0': 'src/glx/libGLX_mesa.so.0.0.0',
    'libgbm.so.1': 'src/gbm/libgbm.so.1.0.0',
    'gbm/dri_gbm.so': 'src/gbm/backends/dri/dri_gbm.so',
    'dri/libdril_dri.so': 'src/gallium/targets/dril/libdril_dri.so',
}
manifest = {}
for name, relative in sources.items():
    source = a.build / relative
    target = lib / name
    target.parent.mkdir(parents=True, exist_ok=True)
    data = source.read_bytes()
    assert data[:4] == b'\x7fELF' and data[4] == elfclass and int.from_bytes(data[18:20], 'little') == machine, source
    shutil.copy2(source, target)
    subprocess.run(['patchelf', '--set-rpath', '$ORIGIN/..' if '/' in name
                    else '$ORIGIN', str(target)], check=True)
    manifest[name] = dict(source=str(source.resolve()),
                         source_sha256=hashlib.sha256(data).hexdigest(),
                         sha256=hashlib.sha256(target.read_bytes()).hexdigest())
for name in ('libdisplay-info.so.3', 'libSPIRV-Tools.so'):
    source = Path('/usr') / libname / name
    shutil.copy2(source, lib / name)
    manifest[name] = dict(source=str(source.resolve()),
                         sha256=hashlib.sha256(source.read_bytes()).hexdigest())
(lib / 'dri/zink_dri.so').symlink_to('libdril_dri.so')

# Cover applications which sanitize loader variables (Steam pressure-vessel
# discovers graphics libraries under /usr), as well as the explicit M4 path.
system = a.output / 'usr' / libname
system.mkdir(parents=True)
for name in manifest:
    target = system / name
    target.parent.mkdir(parents=True, exist_ok=True)
    # Steam's container graphics provider exports /usr libraries. Keep real
    # files there so it never needs a private /opt symlink in the container.
    # Hardlinks avoid duplicating the payload in EROFS.
    os.link(lib / name, target)
(system / 'dri/zink_dri.so').symlink_to('libdril_dri.so')
common = a.output / 'opt/m4-steam'
common.mkdir(parents=True)
for name in ('icd', 'egl'):
    shutil.copy2(Path(__file__).with_name(f'steam-m4-{name}.json'), common / f'{name}.json')
icd = dict(file_format_version='1.0.1', ICD=dict(api_version='1.4.354',
           library_arch='32' if a.arch == 'i686' else '64',
           library_path='/usr/' + libname + '/libvulkan_asahi.so'))
for name in (f'opt/asahi-m4-port/asahi_icd.{a.arch}.json',
             f'usr/share/vulkan/icd.d/asahi_icd.{a.arch}.json'):
    target = a.output / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(icd, indent=2) + '\n')
(a.output / f'opt/asahi-m4-port/mesa-overlay-manifest-{a.arch}.json').write_text(
    json.dumps(manifest, indent=2) + '\n')
print(a.output)
