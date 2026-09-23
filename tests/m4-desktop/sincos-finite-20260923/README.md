# Sin/cos finite-input regression

The AGX3 polynomial lowering emitted its explicit fabs(x)<infinity guard
with the builder's fast-math flags. NIR then folded the guard to false,
selecting NaN even for finite input. Scope preserve_inf and preserve_nan
to that comparison; retain the polynomial's existing optimization policy.

This independently authored shader checks sin(x)^2+cos(x)^2 near one across
32 varying finite angles under shaderFloatControls2. The SPIR-V adds
FPFastMathDefault 458764 (allow transformations while preserving NaN/Inf),
matching the triggering class of shaders. spirv-val --target-env vulkan1.4
passes. The GLSL is explanatory; use the retained exact controlled SPIR-V.

M4 baseline a27cbcbe7bf fails at pixel0 (red instead of green). Candidate
with this change passes all1024 pixels. Both guards armed60s watchdog and
recorded thermal samples (peak83C). No M3 hardware execution.

Rebuild via the copied vulkan-fragment-probe.py helper with --fragment-spirv
finite-controlled.spv --float-controls2 --rgba 0 255 0 255 and the shared
Mesa g17p_offscreen_triangle.c fixture. Execute only under gpu-experiment.py.

The extracted game's independent64x64 table changed from one constant value
to3913 distinct values. It still differs numerically from the software table;
this is not a claim of exact game-shader equivalence or a full-game pass.
Proprietary shaders, captures and game assets remain in the local-only
benchmark repository; none are included here.

## Application validation (M4 only)

With a08e6488f94, the original full Lethal Company DX11/DXVK trace completed
all 1145 checked frames and restored ship lighting. A subsequent real game
launch showed the ship geometry and lighting at 3840x2160. Its live snapshot
was stopped by the host-memory guard and is not counted as a stability pass.

A native M4 recapture of original frames 1115–1145 completed under the
watchdog/thermal guard. Its 31-frame replay passed, with exact RGB equality
between full-trace boundary frames and short-trace frames 1 and 31. No
resource replacement or feature removal was used. Peak temperature was
98.25 C for recapture, 95.75 C for replay qualification. Proprietary traces
and shaders remain solely in the private local benchmark repository.

Capture SHA256: f8f3c267ebbdc6b5df2e2fe722615e25f215fee25df91b9119fb874a14073e74.
This is application regression evidence, not independent Vulkan conformance.
