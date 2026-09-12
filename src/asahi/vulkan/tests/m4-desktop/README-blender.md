# Blender ARM64 test build

`build-blender-arm64.py` reproducibly configures and builds Blender's official
**lite** preset with Vulkan, Python, X11 and Wayland. It does not run the GPU,
install packages, or qualify disabled features. Cycles and optional lite-preset
modules are disabled; validation claims must specify UI/Workbench/Eevee.

First hydrate `release/datafiles/**` using Git LFS in the source checkout. Use
an ARM64 sysroot matching the target, including Python 3.14, Vulkan/Wayland/X11,
OpenColorIO, OpenImageIO, OpenVDB, yaml-cpp, pystring, minizip-ng, blosc,
log4cplus, robin-map, sse2neon and Eigen headers/libraries. This helper deliberately
does not update a machine's packages. Host dependencies include Clang, lld,
Ninja, CMake, Python, pkg-config, wayland-scanner and qemu-aarch64-static.

```sh
python3 tools/build-blender-arm64.py \
  --source reference/blender-5.2.1 \
  --sysroot /home/council/omarchy/graphics-build/sysroot \
  --build build/blender-m4-arm64 --jobs 12
DESTDIR="$PWD/build/blender-stage" cmake --install build/blender-m4-arm64 \
  --prefix /opt/blender-m4
```

`blender-linux-cross.patch` makes CMake find the host Wayland scanner during
cross-compilation, while leaving native builds unchanged. Both CMake's emulator
and `QEMU_LD_PREFIX` are required: some generated build rules run target programs
via binfmt_misc. The helper records its source revision, configuration and sysroot.
It rejects unhydrated startup assets and preserves unrelated source changes.

The first completed build used Blender v5.2.1 commit
`9e2066aef7ef7e20c142ad7bd3303138a4304c93`, GCC target runtime 16.1.1,
and produced executable SHA256
`012b8fb1cacd9fc83f514e2ed57fbe655d177b59a3fa17eddbcad13686e35b98`.
Run UI/Vulkan probes only with `gpu-experiment.py` plus `app-observe.py`, capturing
a software reference first. A successful cross-build or `--version` is not GPU
support or a stability test.
