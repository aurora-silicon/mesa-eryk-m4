# d3d12agx — Direct3D 12 on Apple AGX

A native D3D12 user-mode driver on the Mesa/Asahi substrate: DXIL is
translated straight to NIR and compiled to AGX machine code. **No Vulkan
layer, no SPIR-V, no `dxc` at run time.**

Status at the tip of `dev`: **219 of 334** corpus shaders compile
(127 compute, 64 fragment, 28 vertex); six GPU acceptance suites pass and
`g5_texture` fails on a known, documented wall.

## This repository is not sufficient on its own

It holds the driver and the test *programs* (`tests/*.c`). It holds none of
their *inputs*, and none of the campaign record. You also need:

**`RyanTheTide/ntasp`**, specifically:

| path | what |
|---|---|
| `d3d12agx/testdata/shaders/` | the HLSL + compiled DXIL the tests take as arguments |
| `d3d12agx/scripts/gpusuite.sh` | runs every GPU suite with the right arguments |
| `d3d12agx/scripts/passset.sh` | the corpus measurement |
| `docs/d3d12agx-replication.md` | **start here** — verified build-and-measure recipe |
| `docs/d3d12agx-roadmap.md` | what to do next, ordered by leverage |
| `docs/d3d12agx-campaign.md` | architecture and the binding model |
| `WORKLOG.md` | every session, including the failures |

**System prerequisites** (in no repo): Asahi Linux on AGX hardware, a kernel
whose UAPI matches Mesa 26.1.4, and DirectX-Headers 1.619.1 installed
system-wide at `/usr/include/directx` + `/usr/include/wsl`. The tests
`#include <directx/d3d12.h>` and `<wsl/winadapter.h>` directly.

## Build

```
meson setup _build -Dvulkan-drivers=asahi -Dgallium-drivers= \
                   -Dplatforms=wayland -Dtools=asahi -Dasahi-d3d12=true
ninja -C _build
```

## Branches

| branch | contents |
|---|---|
| `mesa/main` | mirror of upstream `main` |
| `main` | pinned base, `mesa-26.1.4`, no d3d12agx work |
| `dev` | the campaign |

Upstream is `gitlab.freedesktop.org/mesa/mesa` (the `upstream` remote). This
is not a GitHub fork because Mesa has no GitHub mirror to fork from; the
relationship is carried by that remote and by real shared ancestry.

## Licensing

Driver code is MIT, matching Mesa. Anything adapted from Honeykrisp or the
AGX libraries keeps its license and attribution, noted in the commit that
introduces it. **vkd3d-proton and dxil-spirv are LGPL and are used as
reference/oracle only — never copied into this tree.**
