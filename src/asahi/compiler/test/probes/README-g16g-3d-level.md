# M4 explicit 3D texture mip levels

`g16g-3d-level.metal` is compiled offline by AIR-NT 32023.883 on macOS 25G76,
target `applegpu_g16g`. Evidence: build/cts-fixes/native-3d-level.
The main program starts at text-750.bin offset 64. Its sample at offset 94 is:

```
05800cb8b0003900000010100100
```

This uses dimension field 76 = 3, explicit level mode, and field 29 = 256
(level 4 multiplied by 64). It differs from the existing level-zero witness
only in that immediate field. The texture core test checks these exact bytes.
Runtime 3D levels remain outside the accepted shape.

Source SHA256: 24fdfaa139a37ebd50a6ab7976c5135c321d88db9a76f4261d0c92f72d1ccde8
Text SHA256: b564c8f62bab4e88f61390420df535b02f0671e9eb952575f3047cfc07929ed1

Native M4 hardware: 20260913T124006017131Z-cts-3d-level-signed passes the
original CTS 3D RG16 SINT nearest-mipmap select_max_4 case. Full watchdog and
thermal guards; Vulkan library
32e11d82830ae213a9365b393a467d47647cc885127d36a74f741bcaa18c0547.
