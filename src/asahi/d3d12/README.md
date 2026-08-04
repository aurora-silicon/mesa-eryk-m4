# d3d12agx — Direct3D 12 on Apple AGX

A native D3D12 user-mode driver on the Mesa/Asahi substrate: DXIL is
translated straight to NIR and compiled to AGX machine code. **No Vulkan
layer, no SPIR-V, no `dxc` at run time.**

Status on `dev`: **321 of 334** corpus shaders compile, and **all eight GPU
acceptance suites pass on real hardware** — including `g6_geometry`:
**geometry shaders execute**, as two compute dispatches ordered ahead of
the render pass plus a rasterization program bound as the hardware vertex
shader — including `g5_texture`, which
now reproduces `Texture2D.Load` for all 256 texels of a 16×16 image, and
`g4_triangle`, which matches a CPU rasterisation of the shader's own
vertices on all 65536 pixels. The remaining 13 shaders are documented in
[`docs/tier5-hardware-limits.md`](docs/tier5-hardware-limits.md).

The corpus figure is a **compile** proxy. Geometry now dispatches;
tessellation, mesh, amplification, work graphs and ray query still compile
without executing. See `docs/roadmap.md` — that gap is the current work.

## Start here

| doc | what |
|---|---|
| [`docs/replication.md`](docs/replication.md) | build it and reproduce the numbers — **read first** |
| [`docs/architecture.md`](docs/architecture.md) | the stack, the shader path, the binding model, licensing |
| [`docs/roadmap.md`](docs/roadmap.md) | what to do next, ordered by leverage, with falsification criteria |
| [`docs/findings.md`](docs/findings.md) | traps that each cost a wrong hypothesis — read before debugging |
| [`docs/tier5-hardware-limits.md`](docs/tier5-hardware-limits.md) | the last 13 shaders: what each needs, and the two that need a decision |

## Build

```bash
meson setup _build -Dvulkan-drivers=asahi -Dgallium-drivers= \
                   -Dplatforms=wayland -Dtools=asahi -Dasahi-d3d12=true
ninja -C _build
```

Needs Asahi Linux on AGX hardware, a kernel whose drm/asahi UAPI matches
Mesa 26.1.4, and **DirectX-Headers 1.619.1 installed system-wide** — the
tests `#include <directx/d3d12.h>` and `<wsl/winadapter.h>` directly.
Details and the verified procedure are in `docs/replication.md`.

## Test

```bash
src/asahi/d3d12/tests/run-suites.sh            # GPU acceptance suites
src/asahi/d3d12/tests/corpus-passset.sh > now  # corpus compile proxy
```

`tests/shaders/` holds the probe shaders — HLSL sources **and** their
compiled DXIL, committed together because `dxc` does not run on the target
platform. `tests/corpus/` is empty in this repository: those shaders are
LGPL-derived from vkd3d-proton and are regenerated rather than vendored
(see `docs/replication.md`).

## Layout

```
d12_device.c       ID3D12Device, adapter/feature queries, view creation
d12_cmd.c          command lists, draws, dispatches, USC/PPP encoding
d12_queue.c        submission, fences (DRM syncobj timelines)
d12_resource.c     CreateCommittedResource, ail layout selection
d12_descriptor.c   descriptor heaps
d12_pipeline.c     PSO compile, prolog/epilog linking
d12_root_sig.c     root signature serialise/parse (RTS0)
d12_format.c       DXGI ↔ pipe format
dxil/              DXIL → NIR: container, bitstream, module, translator
tests/             GPU acceptance suites, standalone tools, shaders
```

## Branches

| branch | contents |
|---|---|
| `mesa/main` | mirror of upstream `main` |
| `main` | pinned base, `mesa-26.1.4`, no d3d12agx work |
| `dev` | this work |

Upstream is `gitlab.freedesktop.org/mesa/mesa` (the `upstream` remote). This
is not a GitHub fork because Mesa has no GitHub mirror to fork from; the
relationship is carried by that remote and by real shared ancestry.

## Licensing

Driver code is MIT, matching Mesa, SPDX-tagged. Anything adapted from
Honeykrisp or the AGX libraries keeps its license and attribution, noted in
the commit that introduces it. **vkd3d-proton and dxil-spirv are LGPL and are
used as reference and test oracle only — never copied into this tree.**
