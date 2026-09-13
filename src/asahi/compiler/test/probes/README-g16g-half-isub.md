# G16G half-width integer subtraction

Reproduce with:

```
python3 tools/native-metal-compile.py \
  mesa/m4-asahi/src/asahi/compiler/test/probes/g16g-half-isub.metal \
  --output /tmp/g16g-half-isub
```

Captured2026-09-13 using applegpu_g16g, AIR-NT32023.883, macOS25G76.
Source/archive/compiler hashes:build/cts-fixes/native-half-isub/manifest.json.
In text-6f0.bin (main64), offset82 is
`1f01560202040c001705`: ISUB ep0,16-bit destination half2, input halves2/3,
both discarded, wait slot6. The byte fixture in test_native_u16_arithmetic
matches every byte. Addition retains its earlier two shifted fixtures;
shifted subtraction remains rejected until separately witnessed.
