# Optional packed normalized vertex conversion

`HK_NATIVE_VERTEX_NORM8=true` lets the G15/G16 hardware vertex variant decode
packed UNORM8/SNORM8 attributes with the existing native NORMFLT instruction.
The default remains false. The memory read still uses the raw I8 packed load,
with identical byte mask, address, index, shift and coherency. One or two
conversion instructions replace scalar unpack/conversion arithmetic. Swizzles,
missing components and D3D-style robust zero selection are retained. Other
formats, the separate vertex prolog and Gallium paths remain unchanged.
The option participates in pipeline/shader/disk-cache identity, preserving the
full existing compiler option key and the other extended option bits.

Qualification on M3, 2026-10-03:

- All 322 compiler tests pass. The VBO CPU test additionally checks 774 norm8
  lowering/swizzle/bounds cases. Those CPU checks inject decoded floats; they
  do not claim hardware conversion accuracy.
- The pinned Vulkan 1.4 catalog selection contains 500 normalized R/RG/RGB/RGBA/
  BGRA vertex-input cases across pipeline variants: 308 Pass, 192 NotSupported,
  zero Fail or device losses. Unsupported cases remain in the evidence.
  Catalog SHA-256 is
  `72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`;
  original global indices are retained.
- Three Minecraft gameplay images pass the previously authorized one-channel-
  level comparison. Strict exact comparison is retained as rejected: maxima
  are one RGB level, with 25/31/29 changed pixels out of 5,555,088 per image.
  No geometric/edge tolerance or CTS tolerance was added.
- Eighteen alternating same-boot pairs of 30 completed 3024x1837 replay frames
  give median paired gain +0.4104% (15/18 positive). The two nine-pair batches
  give +0.5199% and +0.2920%. Combined separate medians are 174.8715 and
  175.4975 FPS. Final-frame drain is included; setup/warmup/PNG work is excluded.
  These are replay results, not live Java FPS or macOS parity.
- All 36 timing runs verify a 60-second watchdog, 0.5-second all-channel thermal
  telemetry, 95 C startup gate and provisional 110/120 C regulation/emergency
  policy. All complete with clean kernel windows and restored test controls.

Evidence: `gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/vertex-norm8`,
including both timing summaries, strict-image rejection, tolerated comparison,
CTS records, capture inputs and guarded-run receipts. Candidate DSO SHA-256:
`a57134775aea20e27c0341594578c12b2a970432301f6905c2a19693b7db4807`.
Kernel module SHA-256:
`657a4777af05937266b353de6e082a6ba62be00f513893f73f3bc205366db31e`.
The normal desktop build is unchanged. No M4 execution, broad conformance or
live application stability qualification is inferred from these focused runs.
