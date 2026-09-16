# DirectX 11 qualification — J713 / G16G, 2026-09-16

The active user target is DirectX through 11. DirectX 12, ray tracing, and
the unfinished barycentric work are deferred. No additional extension was
needed for this milestone: the committed driver passes the pinned DXVK
capability audit and an actual native D3D11 rendering/compute smoke test.

Mesa baseline: `2ff0cdafc84`; installed library SHA256
`d93caac400ab98ee961e5902976066872b7a3dcb2e4811d85e9de7e16c88a01d`.
DXVK 3.1.1 commit `7df3596eed49cb79f07c18869a1a9a8c067efe04`, with exact
recursive submodule revisions and binary hashes in `dx11-readiness/manifest.json`.
Kernel boot `7ba01d95-ee19-4c0c-9877-e873aaffe7d7`, unchanged during tests.

## Results

| Check | Result | Evidence ID |
|---|---|---|
| Required DXVK feature fields, D3D11.1 gates, required extensions, Vulkan version, push constants | 62/62 pass | `20260916T192058103253Z-dx11-baseline-vulkaninfo` |
| Pinned 1,001-case Vulkan regression | 817 Pass / 184 NotSupported / zero failures | `20260916T192654238296Z-dx11-baseline-regression-r1` |
| Native ARM64 DXVK D3D11.1 device creation | Pass | `20260916T192854684465Z-dx11-native-smoke-r2` |
| D3D11 texture clear, copy, map | All 4,096 pixels exact | same |
| DXBC vertex/pixel shaders, fullscreen triangle, interpolated UV gradient | All 4,096 pixels within one UNORM quantization step | same |
| DXBC compute shader, structured UAV, copy/map | All 256 words exact | same |

The independent oracle also passes on host lavapipe. Both use unmodified
DXVK, which selects descriptor buffers on lavapipe and its legacy descriptor
path on the M4. The test explicitly requests feature level 11_1, identifies
the renderer, and checks every result. HLSL was compiled offline with Wine's
D3DCompile; source and resulting DXBC are retained.

Every M4 run used the mandatory 60-second watchdog and six-channel thermal
monitor. Peak temperature was 67.25 C for the regression and 60.5 C for native
DXVK. The regression used 12 workers and the unchanged 60-second case limit.

The first M4 smoke attempt refused the adapter before D3D11 device creation:
the test incorrectly expected Apple's PCI vendor ID, while Honeykrisp reports
Mesa's vendor ID. The corrected guard requires the Apple M4 device name and
device ID `0x8132`. That failed attempt is retained. Two preliminary reference
attempts lacked SDL WSI configuration; SDL3's offscreen driver works.

## Reproduce

Root `tools/dxvk-feature-audit.py` reads a saved single-device `vulkaninfo`
and the pinned DXVK source. It does not run the GPU. Obtain M4 vulkaninfo
through `tools/gpu-experiment.py` with the explicit installed Asahi ICD.

Build DXVK using Meson, `-Denable_d3d8=false -Denable_d3d9=false
-Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled
--force-fallback-for=libdisplay-info`; initialize submodules recursively.
ARM64 cross configuration is `build/mesa-m4-aarch64.ini`. Build logs and
compiler configuration are archived. Link `tools/d3d11-smoke.cpp` against
`libdxvk_d3d11` and `libdxvk_dxgi`, using DXVK's native Windows/DirectX headers.
Build `tools/d3dcompile.c` with MinGW and `-ld3dcompiler`; run it under Wine
for entries `vs`, `ps`, `cs` from `tools/d3d11-smoke.hlsl`, targeting SM5.0.

Set `DXVK_WSI_DRIVER=SDL3 SDL_VIDEODRIVER=offscreen` and an explicit ICD.
Run `d3d11-smoke {m4|reference} VS.dxbc PS.dxbc CS.dxbc`.
On the M4 this must be inside the watchdog/thermal guard. Exact commands,
guard records, shader files and readback logs are in
`dx11-readiness/qualification.tar.gz`.

## Boundary

This is native ARM64 DXVK without Wine/FEX, a swapchain, or Steam. It establishes
the driver-side baseline for beginning Steam tests, not game compatibility,
desktop stability, all D3D11 optional features, or DirectX 12 support. The
Steam/FEX/muvm runtime still needs qualification with this M4 driver. Existing
blend throughput timeouts under 12-worker contention remain open; this smoke
test does not supersede those CTS records.
