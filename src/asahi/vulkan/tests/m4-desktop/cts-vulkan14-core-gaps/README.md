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
The image tests preceded sample-rate shading qualification below. Its candidate
was present in that library; these compute tests did not qualify sample shading.


## Sample-rate shading and final capability audit

G16G now advertises sampleRateShading and retains actual sample IDs and sample
interpolation. The native linker emits one main/epilog invocation for every
raster sample, preserves per-sample coverage, and acquires pixel dependencies
once across the loop. In particular, releasing class1 after the first sample
lost subsequent stencil updates (the original test read 1 instead of 255).
The independently compiled sample-discard native pipeline holds class1 across
its loop. Keep the existing G17P one-sample boundary unchanged.

Supply the Vulkan default Layer=0 varying when the final hardware vertex stage
does not write Layer. Native multisample input-attachment fetches also accept
NIR's redundant constant LOD0 source. Colorless MSAA uses its reserved 8-byte
sample stride instead of being rejected as an invalid tile layout.

Tile storage and raster sample geometry have independent budgets. Preserve
32 KiB of native color storage, including the previously fixed four RG32F
attachments, while selecting at most 2048 samples per microtile. This matches
the retained J713 native 4x 32x16 geometry. The 32x32/4x configuration stalled
even with sample shading disabled; choosing 32x16 fixed the sample-ID probe
without changing shader arithmetic. This is the qualified scheduling policy,
not evidence of a universal silicon limit or an additional Vulkan API limit.
Deferred sample counts and fragment epilogs retain both budgets. Two focused
host tests pass for full-capacity MRT and the independent sample budget.

Final original-eight plus two color cases:
`20260914T073807816731Z-cts14-core-original-final` — **5 Pass, 5 NotSupported,
0 Fail/Crash**. Of the original eight, three pass and five reach separate
optional requirements: three request 8x/16x raster samples, one requires
multiview tessellation, and one requires transformFeedbackDraw. Both 2x and
4x minSampleShading=1.0 triangle cases pass. The stencil-discard, sample-mask,
and depth-resolve controls all pass at
`20260914T073609169628Z-cts14-core-stencil-bracket`.

The final library SHA256 is
`2141cf24949fd3afd4c93eab9fe35d3ec6a76cb6c9282f051a665a6ca681c996`.
Fresh guarded profile: `20260914T073845272532Z-cts14-core-feature-profile`.
All three repaired features are true; the same registry inventory now reports
**zero unadvertised unconditional core features**. See final-audit.json and
its explicit optional-skip review. The original 1000-case report is unchanged;
this inventory does not establish conditional-feature, format, limit, or full
Vulkan conformance. Final CTS/profile runs had no observed GPU fault, with
peak temperatures 56.5 C/55.75 C. Earlier diagnostic stalls and watchdog
recoveries remain retained in the experiment evidence.

The native sample witnesses and sources are retained under
`build/cts14-core-gaps/{sample-native,sample4-native,sample-discard-native}`.
The sample-ID diagnostic was generated with `vulkan-fragment-probe.py
--samples 4 --sample-shading --rgba 128 0 0 255`. The earlier small-tile
experiment has a `missing_success_evidence` wrapper verdict because its regex
expected `^PASS:`; its actual exact-pixel marker is
`g17p-msaa4-triangle: PASS`, and its exit status was zero. The final CTS results
above provide independently classified validation of the final implementation.
