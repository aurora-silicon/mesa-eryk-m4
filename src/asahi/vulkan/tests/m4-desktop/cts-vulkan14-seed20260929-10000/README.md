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
