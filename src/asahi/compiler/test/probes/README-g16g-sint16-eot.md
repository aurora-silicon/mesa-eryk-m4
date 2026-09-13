# G16G signed RGBA16 end-of-tile witness

Generate and compile without GPU execution:

```
python3 tools/native-eot-input.py 8:4 --output /tmp/sint16.btnf
python3 tools/native-metal-compile.py /tmp/sint16.btnf --stage builtin \
  --compiler /Users/m1/m4-metal-917/Metal.xctoolchain/usr/bin/metal-nt \
  --output /tmp/sint16-native
```

2026-09-13, macOS25G76, AIR-NT32023.917, target applegpu_g16g.
Symbol `_agc.eot` begins at text offset0 and contains68 bytes:

```
1c9c100624a814060c9d102624a910269f1054000004140050060b009f2054020000100050060b009705540400080000d04010005710540000000080a81500000e000000
```

This matches the existing I16 selector0x15 program. G17P's separately qualified
84-byte signed-RGBA16 program must not be selected for G16G. Original Vulkan CTS
case23 (see m4-desktop/cts-1.0-baseline/STATUS.md) validates the resulting bytes
through attachment rendering and ETC2 decompression, without changing the test.
