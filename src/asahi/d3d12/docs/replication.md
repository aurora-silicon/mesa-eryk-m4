# Building and measuring d3d12agx

**Verified by clean-room test** (2026-08-04), not asserted: a bare
`--depth 1` clone of this branch was configured, built and measured with no
reference to the original working tree. It reproduced the pass set exactly —
219 shaders, byte-identical `comm` diff — and the same six green GPU suites
plus the known `g5_texture` failure.

## System prerequisites

These are the part no repository carries, and the part a fresh machine trips
on.

- **Asahi Linux on Apple silicon with a working AGX GPU.** The driver opens
  the `asahi` DRM render node; there is no software fallback. Results here
  are from a MacBook Air M2 (t8112).
- **A kernel whose drm/asahi UAPI matches Mesa 26.1.4.** This branch is
  pinned to that tag deliberately; a mismatched kernel is a real failure
  mode, not a warning.
- **DirectX-Headers 1.619.1 installed system-wide**, providing
  `/usr/include/directx/d3d12.h` and `/usr/include/wsl/stubs/winadapter.h`.
  Build it from `microsoft/DirectX-Headers` and install it (on Fedora, as an
  RPM).

  > This is the **system** install, not a meson wrap subproject. In the
  > clean-room test meson never fetched the `DirectX-Headers.wrap`; the build
  > compiled against `-I/usr/include/directx -I/usr/include/wsl/stubs`. The
  > tests `#include <directx/d3d12.h>` and `<wsl/winadapter.h>` directly and
  > will not build without it.

- Ordinary Mesa build dependencies: meson, ninja, clang/LLVM, libclc, zlib,
  expat, libdrm, wayland.
- **`dxc` is NOT required** to build or to reproduce these numbers, and is not
  needed on the target machine at all. It is only needed to *author new* probe
  shaders, which is why the compiled `.dxil` are committed next to their
  `.hlsl` in `tests/shaders/`.

## Build

```bash
meson setup _build -Dvulkan-drivers=asahi -Dgallium-drivers= \
                   -Dplatforms=wayland -Dtools=asahi -Dasahi-d3d12=true
ninja -C _build
```

Products in `_build/src/asahi/d3d12/`: `libd3d12agx.so`, the GPU acceptance
tests (`d3d12agx_g*`), and two standalone tools — `d3d12agx_dxil_compile`
(DXIL in, AGX disassembly out) and `d3d12agx_dxil_dump` (container/bitcode
inspection).

## Measure

### GPU acceptance suites — run these after every change

```bash
src/asahi/d3d12/tests/run-suites.sh [build-dir]
```

Expected today: six suites `ALL PASS`, and `g5_texture` failing on the known
wall (§1.1 of `roadmap.md`). The script takes the build directory as an
argument and finds its shaders relative to itself, so no path is baked in.

It treats a **missing verdict line as a failure**: a test given a wrong
filename prints usage and exits 0, which any harness checking only the last
line would read as a pass.

### The corpus — the compile-only proxy

```bash
src/asahi/d3d12/tests/corpus-passset.sh [corpus-dir] [dxil_compile] > now.txt
```

It prints the sorted **set** of shader names reaching `NIR->AGX OK`. Compare
sets, never counts:

```bash
comm -23 before.txt now.txt    # regressions -- must be empty
comm -13 before.txt now.txt    # newly passing
```

A change that trades one shader for another moves the count by zero.

## The corpus is not in this repository

`tests/corpus/` is empty here. The 334 shaders are compiled from
**vkd3d-proton's test HLSL** (`tests/shaders/**/*.hlsl`), which is
**LGPL-2.1**, and this tree's standing rule keeps vkd3d-proton as a reference
and test oracle only — never copied in. See `architecture.md` §
"Licensing boundaries".

To regenerate: compile each `tests/shaders/**/*.hlsl` from a vkd3d-proton
checkout with `dxc`, at the profile encoded in the filename (e.g.
`bindless_cbv.cs_5_1.cs_6_0.hlsl` → `-T cs_6_0`), writing
`<basename>_code_dxil.dxil`.

> **Known gap.** The exact extraction that produced the current 334 was not
> recorded at the time. Re-deriving it and confirming the result matches by
> name and by pass/fail behaviour is a small, worthwhile task — it turns the
> corpus from an artifact into a reproducible input.

**So: the GPU suites replicate anywhere; the 219 figure replicates only on a
machine that has the corpus.**
