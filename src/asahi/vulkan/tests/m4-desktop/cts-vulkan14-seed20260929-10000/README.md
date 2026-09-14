# Vulkan 1.4 random sample, seed20260929

Frozen broad Linux-only sample:10000 cases out of3206030 eligible entries in
CTS1.4.6.2, selected with Python random.Random(20260929).sample. Case-list SHA256
5bf35cce436384ed9cad34d0f90110b814a6e57470e6cc1d3987335b28fa2aca.
Optional skips are retained separately; requirements still need review.

Initial20-worker run stopped after a userspace SIGSEGV:355Pass,294NotSupported,
2Fail,1Crash,29Interrupted,9319Unrun. Runner time20.132s; peak62.75C, watchdog60s.
No GPU fault. Original outcomes remain in run1-summary.json and workspace QPAs.

The scaled RGBA8 swizzle case crashed inside CTS createShaderModule. Its table
compiled PROGRAM_2D_UINT but runtime getSamplerType correctly selected the
floating-point program for USCALED. The missing binary caused the crash.
The same table error affected17 scaled formats. Use FLOAT/FLOAT_BIAS programs;
retain texture formats, swizzles and pixel comparisons. The original case now
passes on M4 (guarded180058, runner0.408s, peak58.5C).

cts-scaled-swizzle.patch is cumulative with the explicitly documented seed28
CTS corrections. Source baseline:f6a29701220f34dd1407513bfe80d74ca7b392ce.
Build using tools/cts-patched-build.py with reference/vk-gl-cts-1.4.6.2 and
build/cts-1.4-arm64. The build manifest records executable/patch hashes and
restoration of original source files. This is an adapted diagnostic CTS build,
not an unmodified conformance submission. Remaining cases/fixes are in progress.

## Split depth/stencil copies and sparse queries

Continuation run2 completed another 4,140 cases before a Mesa assertion in
`depth_stencil_msaa_copy.partial.d24_unorm_s8_uint_D_2_bit_bind_offset`.
The graphics copy properties still described interleaved D32/S8 and omitted
D24/S8 entirely, although image allocation splits both into depth and stencil
planes. Use one-component raw views of each physical plane, including D24 float
storage; preserve aspect selection and partial multisample copies.

Two sparse image format queries also contradicted the advertised sparseBinding
feature. Reject sparse image flags when sparseBinding is false, including through
the shared sparse format query path. No feature advertisement was changed.

All three original cases pass with installed Mesa SHA256
`16ae1f0c91889e3e4bf4deb8c9157ae83c793f504f12045681cf48e65e0081ef`,
0.609 seconds runner time, peak 58.25 C, watchdog guard pass.
Evidence: `20260914T180951615594Z-cts29-depth-sparse-fixed`.
The full frozen sample remains incomplete; no broad regression was run.
