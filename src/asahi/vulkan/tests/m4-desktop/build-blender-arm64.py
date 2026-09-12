#!/usr/bin/env python3
"""Cross-build Blender's official lite preset with Vulkan for guarded UI tests.

Uses an existing ARM64 sysroot and hydrated Blender source; never runs the GPU
or installs packages. Lite disables Cycles and optional modules, so this build
can qualify UI/Workbench/Eevee, not full Blender feature support.
"""
import argparse, json, os, shutil, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--sysroot',type=Path,required=True)
p.add_argument('--build',type=Path,required=True)
p.add_argument('--jobs',type=int,default=12)
p.add_argument('--python-version',default='3.14')
a=p.parse_args()
source=a.source.resolve(strict=True);sysroot=a.sysroot.resolve(strict=True);build=a.build.resolve()
assert a.jobs>0
for name in ['clang','clang++','ninja','cmake','qemu-aarch64-static','wayland-scanner']:
 assert shutil.which(name),name
assert (source/'release/datafiles/startup.blend').read_bytes()[:7]==b'BLENDER','Hydrate release/datafiles with Git LFS first'
for asset in (source/'assets').rglob('*.blend'):
 with asset.open('rb') as f: prefix=f.read(128)
 assert not prefix.startswith(b'version https://git-lfs.github.com/spec/'),f'Hydrate assets/** with Git LFS: {asset}'
gcc=list((sysroot/'usr/lib/gcc/aarch64-unknown-linux-gnu').glob('*'))
assert len(gcc)==1,'Select a sysroot containing one target GCC installation'
for name in ['blender-linux-cross.patch','blender-wayland-color.patch']:
 patch=Path(__file__).with_name(name).resolve()
 if subprocess.run(['git','-C',str(source),'apply','--reverse','--check',str(patch)],capture_output=True).returncode:
  subprocess.run(['git','-C',str(source),'apply','--check',str(patch)],check=True)
  subprocess.run(['git','-C',str(source),'apply',str(patch)],check=True)
build.mkdir(parents=True,exist_ok=True)
# Quote CMake paths; this helper rejects syntax-bearing path characters.
for path in [source,sysroot,build,*gcc]:assert not any(c in str(path) for c in '\n";$')
toolchain=build/'m4-toolchain.cmake'
toolchain.write_text(f'''set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT "{sysroot}")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_C_FLAGS_INIT "--gcc-install-dir={gcc[0]}")
set(CMAKE_CXX_FLAGS_INIT "--gcc-install-dir={gcc[0]}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_FIND_ROOT_PATH "{sysroot}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_CROSSCOMPILING_EMULATOR {shutil.which('qemu-aarch64-static')};-L;{sysroot})
set(ENV{{PKG_CONFIG_SYSROOT_DIR}} "{sysroot}")
set(ENV{{PKG_CONFIG_LIBDIR}} "{sysroot}/usr/lib/pkgconfig:{sysroot}/usr/share/pkgconfig")
''')
cmd=['cmake','-S',str(source),'-B',str(build),'-G','Ninja','-C',str(source/'build_files/cmake/config/blender_lite.cmake'),'-DCMAKE_TOOLCHAIN_FILE='+str(toolchain),'-DCMAKE_BUILD_TYPE=Release','-DWITH_LIBS_PRECOMPILED=OFF','-DWITH_VULKAN_BACKEND=ON','-DWITH_INSTALL_PORTABLE=ON','-DWITH_PYTHON_INSTALL=OFF','-DWITH_PYTHON_NUMPY=OFF','-DPYTHON_VERSION='+a.python_version,'-DPYTHON_EXECUTABLE='+shutil.which('python3'),'-DWITH_GHOST_WAYLAND_LIBDECOR=OFF','-DWITH_GHOST_X11=ON']
(build/'m4-build-manifest.json').write_text(json.dumps(dict(command=cmd,source_head=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip(),preset='lite; no Cycles',sysroot=str(sysroot)),indent=2)+'\n')
subprocess.run(cmd,check=True)
# Some Blender build generators invoke target executables through binfmt_misc
# instead of CMAKE_CROSSCOMPILING_EMULATOR; both must use the same target loader.
subprocess.run(['ninja','-C',str(build),'-j',str(a.jobs)],env={**os.environ,'QEMU_LD_PREFIX':str(sysroot)},check=True)
