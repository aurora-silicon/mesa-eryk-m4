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

Sample-rate shading and image robustness remain pending.
