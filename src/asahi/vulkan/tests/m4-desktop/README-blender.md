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

`blender-wayland-color.patch` fixes the first real startup failure: only claim
Wayland color-management ownership when that surface actually supports the
R16-float/passthrough swapchain. Otherwise the SDR Vulkan WSI path owns it.
It also checks failed surface-format/present-mode queries before using counts;
surface loss previously caused an uninitialized allocation size and OOM/abort.
The patched binary `e565309e5dbc029ad5d8508f9d12d840258af6bbd24cc235b63d1e54da3b8e20`
opened on lavapipe in `20260912T213109755531Z-blender-vulkan-color-ownership-reference`,
retaining a nine-frame Vulkan capture. This establishes startup, not yet a clean
viewport or M4 support. Also hydrate `assets/**` (separate from release/datafiles)
before installing; otherwise Blender reports Git LFS pointers as invalid assets.

### Native Blender vertex storage requirement

Blender requires vertexPipelineStoresAndAtomics at startup. G16G now exposes its
qualified vertex storage path by default as well; other chips retain the
upstream M1-derived DRI policy. The prior paragraph records an intermediate
state, superseded by this change. Native Vulkan Workbench startup and its cube,
grid and axis rendering complete in
`20260913T002807435127Z-blender-native-viewport-vertex-enabled`, peak56.75 C.
The narrow tiled UI still needs a full-width check; this is not Eevee or Cycles
qualification. The fresh software capture has malformed grid lines that the
native run does not, so it is not a clean pixel correctness reference.

`blender-wayland-initial-resize.patch` fixes an application-side null dereference
when the first fullscreen configure arrives before the Vulkan window-info
allocation. Like EGL, Vulkan now defers its backend resize until allocation;
the constructor initializes the latest negotiated frame size. The retained
00:29 crash is in gwl_window_resize_for_backend, not a GPU reset.
The rebuilt executable SHA256 is
`447cd64788bde92bdf034aa32d9afc0a8c2c72746f6aee9626bbc1f61a78b4e6`.
Native fullscreen startup and a timed cube rotation complete in
`20260913T003232545143Z-blender-native-fullscreen-resize-fixed`, peak57 C.

## Eevee qualification scene

`blender-eevee-scene.py` runs through Blender --python inside app-observe.py
and gpu-experiment.py. It saves the exact .blend, script hash, build options,
renderer metadata and final PNG under the runner's diagnostics/eevee directory.
The initial192x144,16-sample scene contains a diffuse cube, metallic sphere,
ground, world fill and an area light casting shadows. Settings are identical
between renderers. PASS means rendering finished; image comparison is separate.

The first lavapipe capture20260913T020724886404Z completed in34 seconds with
peak59.25 C; its PNG shows both objects, highlights and shadows. The206-pipeline
capture is retained for bounded M4 replay; native Eevee is not qualified yet.
The build audit retains all actual bpy.app.build_options. Cycles, OpenVDB and
other lite-preset modules are absent; enabling Eevee does not close those gaps.
