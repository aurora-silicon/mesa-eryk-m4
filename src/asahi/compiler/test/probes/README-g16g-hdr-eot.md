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

## Two packed HDR targets

The two-attachment BTNF input keeps both attachments in partition 0 and mask 3.
The native G16G output is the same 120-byte envelope as the existing
{RGBA16F, R8} reference, with only byte 101 changed from 0x15 to 0x75.
Regenerating the existing mixed-width reference is byte-identical. Inputs with
partition 1 were exploratory controls and are not the supported tile layout.

The two packed pixels occupy offsets 0 and 4 in an 8-byte sample. Bind both
PBE descriptors and both imageblock offset arguments. For background clears,
upload both complete vec4 values and reserve uniform halves 4 through 19;
the scalar RT1 clear path previously reserved only through half 13. The generic
uniform loader builds the body into its checked output capacity; the old
single-clear byte-count maximum is not an architectural limit. Background
shader size likewise uses the compiler container and loader span validation,
not the previous arbitrary 256-byte admission limit.

`hdr-attachment-probe.c` is a desktop GL oracle (EGL, GL, libm). It checks R16F,
RGBA16F, and one/two R11G11B10 attachments through clear, draw, additive blend,
and reload. Each 128x64 readback checks every channel against exact test values,
including HDR values above one. Half-float readback avoids an unrelated missing
RGBA32F render-target path; it does not disable any operation being tested.
Host llvmpipe and J713 private Zink both pass all 15 readbacks (122,880 pixels),
with zero bad channels. No per-draw synchronization override is used.

Hardware evidence: _provenance/experiments/
20260912T020735229728Z-hdr-attachments-half-readback. Installed Vulkan SHA256
3bf3ceef76e3bd8702848d7785ef42361066244b16fb1d0e265fc22158542e0c.
Watchdog60 and six-channel thermal guard remained enabled; peak 54.5 C.
The first F32-readback diagnostic failed on the RGBA32F conversion target and
is retained separately, not counted as passing. A game race is still a separate
qualification target. Host native-EOT byte tests and 6,778 Ioto checks pass.
