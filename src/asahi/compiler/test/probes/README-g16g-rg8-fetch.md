# G16G RG8 framebuffer fetch

```
python3 tools/native-metal-compile.py \
  mesa/m4-asahi/src/asahi/compiler/test/probes/g16g-rg8-fetch.metal \
  --stage render --function fs --pixel-format RG8Unorm \
  --output /tmp/rg8-native
```

2026-09-13, target applegpu_g16g, default AIR-NT32023.883 on macOS25G76.
Retained archive/commands/hashes:build/cts-fixes/native-rg8-fetch.
The fragment text is146 bytes; `_agc.main` starts at64. At offset76:

```
670e5400000001d202000000
```

LLDCOL has width0 (W8), channel mask3, destination half0, completion slot6,
first-load1, sample-mask1. It returns the two bytes packed in one half-register.
The existing normflt lowering unpacks each component after the load.
`agx3_tile_core_test` compares the exact native instruction.
