# G16G shader layer and viewport outputs

Enable `shaderOutputLayer`, `shaderOutputViewportIndex` and the corresponding
`VK_EXT_shader_viewport_index_layer` extension on G16G. The previous gate
predated native packed layer/viewport exports, layered storage, and multiple
viewport state emission. Those paths are already implemented and qualified
for other uses. The remaining fragment-input validation omission rejected
`VARYING_SLOT_VIEWPORT`; it now accepts that flat varying with the existing
component/UVS bounds checks, alongside `VARYING_SLOT_LAYER`.

Qualification uses the frozen 3,206,030-case catalog, SHA256
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
All hardware runs retain the 60 s watchdog, six thermal channels every 0.5 s,
95 C cutoff, 12 CTS workers and fixed 60 s per-case bound. The CPU thermal
policy retained lower caps; these timings are not full-clock benchmarks.

The temporary gated library SHA256
`b244098ab5f222493494c0637c6b91b7c87bebc472fe747aa81fd3b10582906d`
passes the five initial vertex/tessellation/fragment cases
(`20260916T124101103051Z-dx-layer-first-five`, peak 61 C), then all 3,569
selected cases: **1,516 Pass / 2,053 NotSupported / zero failures**
(`20260916T124300357595Z-dx-layer-all-r1`, peak 65.25 C).
Skips are 1,836 device-generated-command cases, 208 exclusive-transfer-queue
cases and nine format/sample-count combinations, not layer/viewport defects.
All 252 focused draw cases are included. They also compile/record through the
host shim, where pixel comparisons necessarily fail because the shim does not
render. The pre-fix fragment viewport case instead failed command recording.

Independent `shader-layer-viewport-probe.c` checks six configurations: vertex
and tessellation exports, selecting layers, viewports or both. The fragment
shader reads both builtins, and exact comparisons include every untouched
pixel in the four-layer/four-viewport combination. All six pass on llvmpipe
and M4 (`20260916T124521732629Z-dx-layer-oracles-r2`, peak 62 C).
The first probe requested a 4.5 context while `run-m4-gl.py` explicitly forces
3.3, so it failed before rendering. The corrected probe uses GLSL 330 and
the required extensions, preserving the same routing and comparison. The
original source and failed evidence are retained; this was not a driver fix.

The default-enabled library SHA256
`97b9ef40f6da6862cd0d67f93bf04727d20264ae227830555193805acc3716b9`
passes all 4,570 selected plus regression cases: **2,331 Pass / 2,239
NotSupported / zero failures**
(`20260916T124623528461Z-dx-layer-default-regression`, peak 65 C).
Host suite: 104 Pass / the same 11 known failures. Host and ARM64 builds pass.
Indexed selections, exact result records, probe references and failures are
under `layer-viewport/`. The CTS binary is the documented 1.4.6.2
`deqp-vk-minmax-components` build; its sampler/scaled-blit corrections do not
alter these layer/viewport comparisons. This is API qualification, not a
Steam application validation claim.

All six probes also pass on the default build (`20260916T124938282538Z-dx-layer-default-oracles`),
peak 61.25 C.
