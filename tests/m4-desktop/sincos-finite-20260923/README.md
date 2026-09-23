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
