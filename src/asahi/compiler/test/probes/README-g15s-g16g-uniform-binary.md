# Native G15S/G16G uniform FP32 binary arithmetic

This own Metal source was compiled offline with Xcode 27.0/27A266a,
AIR-NT 32023.921, `-fno-fast-math`, explicit applegpu_g15s and applegpu_g16g
targets, one RGBA8Unorm color attachment and no GPU execution. The complete
198-byte fragment text (including the 64-byte constant program) is identical
on both targets, SHA-256 5ae7fcafe5e07b55c5cca77d5d4ad4fa093ad4770518bd38ee36dffe2ca1e79d.
The hex retains full text; instruction offsets are from its beginning.

| Offset | Instruction bytes | Fields |
| --- | --- | --- |
| 0x78 | 098325050002 | FMUL r0, u2, r1; dest cache, register live |
| 0x7e | 498524070002 | FADD r4, u3, r2; dest cache, register live |
| 0x8e | 19032c09000a | FADD r1, -u4, r1; dest cache, register last use |
| 0x94 | 29052d0b0002 | FMUL r2, u5, r2; dest cache, register last use |

Anonymous field f36 names the uniform half index (4/6/8/10), with f42=3.
The GPR operand remains f18. Uniforms have no GPR cache/discard hints.
Every decoded candidate re-encodes byte-for-byte through the existing generated
composer. These are native compiler witnesses, not Linux hardware execution
or proof of an application performance improvement. Raw compiler identities,
archives and field decodes are retained in build/m3-wide-preamble-20261003.
