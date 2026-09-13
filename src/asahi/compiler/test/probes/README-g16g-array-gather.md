# G16G indexed array gather with offsets

```
python3 tools/native-metal-compile.py \
  mesa/m4-asahi/src/asahi/compiler/test/probes/g16g-array-gather.metal \
  --stage render --function fs --pixel-format RGBA32Sint \
  --output /tmp/array-gather-native
```

2026-09-13, applegpu_g16g, AIR-NT32023.883, macOS25G76.
Evidence:build/cts-fixes/native-array-gather-indexed (full source, archive,
tool commands and hashes). Fragment text210 bytes; main starts at64.

- At150:PACK_SMPIR `1725540600100000f0e40400`, layer half8, slot6, selector4.
- At162:SMP `05800cb414000103014280010108a02800004000`, register texture0,
  sampler4, array mode2, gather component2, signed XY offsets0x88(-8,-8),
  completion slot1, staging wait6.

The unit fixture preserves those operand fields and clears native f156
(byte8 bit0) for Mesa's existing helper-retention policy across multiple
gathers. That fixture is intentionally not an unmodified native instruction.
The full NIR compile test is `agx3_graphics_compile_test --array-gather-only`.
Original CTS case48 validates the multiple-offset integer-array/swizzle result.
