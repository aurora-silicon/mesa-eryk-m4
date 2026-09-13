# G16G compute integer fetch with runtime LOD and image store

```
python3 tools/native-metal-compile.py \
  mesa/m4-asahi/src/asahi/compiler/test/probes/g16g-integer-copy-lod.metal \
  --output /tmp/integer-copy-native
```

2026-09-13, applegpu_g16g, AIR-NT32023.883, macOS25G76.
Evidence:build/cts-fixes/native-integer-copy-lod (source/archive/tool hashes).
Compute text264 bytes; main128. Relevant native instructions:

-146:PACK_SMPIR `1705541600280004f0400c00`, integer LOD from uniform, mode3.
-162:SMP `05800cb9b0008024000000000200`, register LOD selector4.
-244:IMGWCI `d72054002040000100040000881a0900`, integer F70=0 input lanes.

The PBE supplies destination component width and signedness. CTS index19
validates signed RGBA16 output across its extended mip/layer cases. The NIR
regression is `agx3_graphics_compile_test --compute-lod-store-only`.
