# Finite half-float framebuffer overflow — M4, 2026-09-25

Satisfactory DX11 stores scene depth in the alpha component of RGBA16F normal
textures. Finite f32 values overflowed to infinity on conversion, unlike the
finite saturation used by the software renderer. The subsequent bilateral
normal weighting produced NaNs and a large white lighting artifact.

Vulkan Floating-Point Format Conversions permits finite overflow to round to
the largest finite value or to signed infinity:
https://docs.vulkan.org/spec/latest/chapters/fundamentals.html#fundamentals-fp-conversion
This is an allowed implementation policy change for application compatibility,
not a claim that the previous overflow choice violated Vulkan.

Saturate finite f32 values when writing half-float framebuffer formats, after
blending and before tile/spilled storage. Integer classification preserves
actual infinities, NaNs and signed zeros regardless of shader fast-math modes.
Shader arithmetic conversions and storage-image writes are unchanged.

`probe.c` uses Zink over the native M4 Vulkan driver. Compile for the target with
`cc -O2 probe.c -lEGL -lGL -lm`. Run only under the mandatory experiment guard.
It checks 16 values across four channels with and without additive blending:
finite positive/negative overflow, finite boundary values, actual infinities,
NaN and signed zero (unblended). Before: 56/128 policy mismatches; after: 0/128.
The baseline is a policy regression, not a standalone Vulkan conformance fail.

All six selected host blend/MRT/sRGB/f16-IO tests pass. Original DX11 menu replay
completes all 31 frames without replacements or memory rebinding (evidence
20260925T083841195293Z-satisfactory-dx11-menu-finite-full31). Against the adapted
lavapipe image, mean RGB error falls from 75.299 to 1.487/255; remaining numerical
and raster differences preclude a bit-exact cross-driver claim. Readback at draw
123679 confirms the input texture has zero infinities (formerly 156081 alpha
values) and the output has zero NaNs (formerly 30650 pixels in all RGB channels).
Evidence: 20260925T085119267382Z-satisfactory-finite-overflow-input-validation.
Game assets, shader binaries and rendered images remain in the private local
benchmark repository. M3 was not tested, per the user's task scope.

A short original DX11 live run with this candidate also reached the animated
menu with the factory and lighting visible, after dismissing the offline login
timeout. No shader replacements were used. Evidence:
20260925T085159328386Z-satisfactory-dx11-menu-finite-live. The guard passed,
with 752 thermal samples and a 110 C peak. This is visual validation, not
a performance measurement or qualification of the other renderer modes.
