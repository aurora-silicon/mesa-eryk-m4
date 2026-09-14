# Vulkan 1.4 mandatory capability gaps

The seed-20260925 audit identified multiview, sampleRateShading and
robustImageAccess as unadvertised unconditional core requirements. Preserve
the original sample/audit; this directory records targeted implementation work.

## Multiview

G16G now exposes the existing per-view draw lowering through native layered
UVS. Background reloads must read the native auxiliary render-target layer
(SR12, 16-bit), not the compute threadgroup Z (SR28, 32-bit). The working
native layered EOT witness reads `24c31046`, independently decoded as
`rld half4, SR12, slot3`. Shader fragment layer inputs retain their flat
varying path. EOT compute wrappers retain their existing native lowering.

Non-layered auxiliary PBE descriptors name one layer even when the API
attachment view spans several. The typed EOT path also handles single layered
integer targets through the same selectors as MRT.

Hardware evidence: `20260914T061536900219Z-cts14-core-multiview3`,
Mesa library SHA256 `1416a1f6a639f670cc6123ffd7c5402a67af1c1126bdcc7c579f3f504e32531b`.
Four targeted passes include indexed multiview, sparse mask preservation,
six separate clear layers and the original no-draw R8 UINT case. Two original
cases now skip on optional multiview tessellation and transformFeedbackDraw.
Watchdog 60 seconds, peak 56.75 C, no observed GPU fault. See retained results.

## Robust image access and explicit gradients

G16G now advertises robustImageAccess. NIR guards image loads/stores and
integer texel fetches against the API view dimensions before coordinate
narrowing. Invalid reads return zero; invalid writes do not execute. Internal
meta shaders retain their disabled-robustness policy. robustImageAccess2 is
still not advertised.

The sampled-image tests also require textureGrad. Implement native 2D, array
and 3D explicit derivatives using the interleaved F32 staging operand bank
and SMP gradient mode, preserving anisotropic sampling semantics. Exact 2D
and 3D instruction witnesses come from AIR-NT 32023.883 targeting G16G;
retained sources/archives are under build/cts14-core-gaps/gradient*-native*.
The native indexed 3D witness is in gradient3d-indexed.

3D gradients require a full 32-half staging allocation. The same instructions
with 20 or 24 allocated halves return stale values; 32 passes the distinct
XYZ/border/gradient probe. Reserve the complete bank in scoreboard tracking,
including its implicit span, alongside the eight result halves.

All eight selected CTS image_robustness cases pass: sampled and storage R32F,
2D, 2D array, 3D, and 4x multisample 2D, compute stage. Evidence:
20260914T071515168901Z-cts14-core-robust-final, library SHA256
7506b970dd5341ddfa93e1018205f26676c3b7b82370bc3ddc97d558e9e0392a.
The bank probe passes at 20260914T071057935679Z-cts14-core-gradient-bank.
Both runs used the 60-second watchdog and six-channel thermal monitor;
peak temperatures 56.5 C and 55.75 C, no fault in these final runs.
Host texture encoding (6302 checks) and scoreboard (64 checks) tests pass.

These are focused feature tests, not a conformance claim or broad regression.
Sample-rate shading remains pending; its uncommitted candidate was present in
the tested library and is not qualified by these compute tests.
