# G16G cube-array sampling

```
python3 tools/native-metal-compile.py \
  mesa/m4-asahi/src/asahi/compiler/test/probes/g16g-cube-array.metal \
  --output /tmp/cube-array-native
```

2026-09-13, applegpu_g16g, AIR-NT32023.883, macOS25G76.
Retained source/archive/commands/hashes:build/cts-fixes/native-cube-array.
Compute text206 bytes; main64. At162 the native packed cube-index/face operand
is `1705540600162800f0c02900`: cube index half11, face half10, bank6, slot1,
first-source mode4 and second-source mode10/12. At174, SMP is
`05800cb8b0005300000010000100`: dimension4, array2, immediate LOD0.
The texture-core test matches both instructions exactly.

Keep native cube-array descriptors on G16G and preserve rounded/clamped cube
indices from normal NIR array lowering. Project XYZ to UV/face while retaining
the index as a fourth coordinate. The native auxiliary operand combines the
index and face; no flattening into a 2D-array sample is needed.
Runtime-LOD cube arrays remain explicitly unsupported until their three-part
auxiliary packing is established. Implicit and immediate-LOD forms compile in
`agx3_graphics_compile_test --cube-array-only`; existing cube tests also pass.
