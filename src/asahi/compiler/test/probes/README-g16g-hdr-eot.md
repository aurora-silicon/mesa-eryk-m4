# G16G half-float and packed HDR EOT

Native `metal-nt` 32023.917, target `applegpu_g16g`, macOS 26.0/26.0,
compiles the adjacent BTNF inputs through the genuine EOT factory. The reference
RGBA8 input uses vendor union 5 and EOT configuration union 4. Only attachment
format/component fields differ in these RT0 witnesses; partition 0, layout 2,
mask 1, one attachment are retained.

The local native G16G B0 UMD's named TextureFormat constants identify F16 as
internal format 12, R11G11B10 as 21, and F32 as 17. In the arm64e slice,
texFormatR16F / RG16F / RGBA16F are at 0x7c99e8 / 0x7c9a48 / 0x7c9aa8;
texFormatR11FG11FB10F is at 0x7cbde8. Their internal format dword is at +32,
component count at +28. These agree with the known RGBA8 format 3 and R32F
format 17 references. This is a native format-table observation, not an
assumption that Mesa's ISA format enum has the same numbering.

All four texts are 68 bytes with the existing RT0 PBE-table/offset binding.
F16 with 1, 2 or 4 components emits byte-identical text ending with IMGW selector
0x15; packed RGB emits selector 0x75. The PBE retains the logical format and
component count. Keep F16 typed through instruction selection instead of
rewriting it to I16, which loses the distinction from integer EOT keys.

| Input | Input SHA256 | Text SHA256 |
| --- | --- | --- |
| f12-c1 | `d96a28f3b3923d9b8f78bc3dd72d4aca435972ea6dd05bc0892d054dac405a6b` | `a057ad91d3991858f1d27e77eac2c83e872a18dfa59463b77e727b6b0894a602` |
| f12-c2 | `b9bdcc54b852891a1e0658c44ca36e4cbe92b29d71a427fe23cf863063d5df9a` | `a057ad91d3991858f1d27e77eac2c83e872a18dfa59463b77e727b6b0894a602` |
| f12-c4 | `e2fe88df6df2a8671cd3c217d6edaec61f69aa474946741f84498fae2759d001` | `a057ad91d3991858f1d27e77eac2c83e872a18dfa59463b77e727b6b0894a602` |
| f21-c3 | `3c5297988fd50e359f1e7124eb76bec0169073de411d5a3a5e3c8e7f2cc94548` | `e641858631bd55fb938f839920beb6b2921a8bbada65bd72b21102daed9651de` |

Workspace evidence is in build/native-eot-f12-c{1,2,4} and build/native-eot-f21-c3,
including commands, versions, complete archives, extracted text and symbols.
Use tools/native-metal-compile.py INPUT --stage builtin --compiler ABSOLUTE_MAC_PATH
--output NEW_DIRECTORY. The required compiler was exported as Metal Toolchain
27A5194o and mounted read-only on the authorized build host. Its default 32023.883
compiler lacks this builtin factory and returns failure; the older wrapper then
crashes while dereferencing that missing result. Use the explicit newer path.

The captured 664-byte SuperTuxKart F16 EOT input now compiles to the identical
68-byte program offline. The focused graphics --eot-only tests compare native
bytes/resources and retain unsupported-format rejection. Hardware rendering
qualification remains separate; these changes alone do not qualify an STK race.
