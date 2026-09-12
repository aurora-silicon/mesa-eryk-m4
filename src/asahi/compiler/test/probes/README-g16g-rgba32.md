# G16G RGBA32 and imageblock stride

Firefox WebRender startup requests RGBA32F and RGBA32I attachments. The
native path rejected these 16-byte tile layouts. The three adjacent BTNF
inputs use the native G16G format-table IDs, layout2, partition0, mask1 and
four components. Compile using native-metal-compile.py and Metal32023.917.
All three outputs are the existing 68-byte full-width I32 EOT, selector0x35.
RGBA32 integer IDs10/11 differ from RG32 integer IDs18/19; reusing the latter
would split the values into 16-bit pieces.

| Attachment | Native format ID | BTNF SHA256 | Text SHA256 |
| --- | --- | --- | --- |
| RGBA32FLOAT | 17 | `fdd7683b141c894bf619d7f5cd06ad404f3f79b5c86b8e2919d29e0e9914af9a` | `60beeb253d8f81af993e93617ed3fca4559e0f614c8465f4cd2bb42c0f1f5fb6` |
| RGBA32SINT | 10 | `e3db64a74e28cf2c6b69bd8bdc82c73b78cef4cff42d3fa80b1926b80c8eb79e` | `60beeb253d8f81af993e93617ed3fca4559e0f614c8465f4cd2bb42c0f1f5fb6` |
| RGBA32UINT | 11 | `d62ce077c8186f33876e05587a7ae02f1574aad5c9309bc0e57e1cd159ab7f90` | `60beeb253d8f81af993e93617ed3fca4559e0f614c8465f4cd2bb42c0f1f5fb6` |

The first hardware attempt enabled the EOT and format admission but retained
the previous constant SPECLM. RGBA32F preserved R/G but returned zero B/A,
both through GPU meta-copy and Vulkan host-image-copy readback. The fragment
lstcol byte sequence matches native G16G RGBA32Float compilation. Evidence:
`20260912T033039146791Z-rgba32-attachments` and
`20260912T033716890657Z-rgba32-hostcopy` (both failures, retained).

The native imageblock encoder identifies a surrounding-state mismatch:
SpecLM::encodeImageBlockData (arm64 UMD0x315428) computes stride/8 in semantic
hi[39:35]. AGX3EncodedInstr<SPECLM_0> (0x2a7834) packs that into instruction
bits[29:25]. Every prior fragment/background/EOT declared8 bytes regardless
of the tile layout. The corrected descriptors carry the actual sample size
through all three paths. Attachmentless/default descriptors retain the prior
8-byte declaration; geometry and existing coverage handling are unchanged.

`speclm-imageblock.py` executes these exact native routines offline, with
synthetic owned memory and a strict UMD hash. It independently reproduces:

| Sample bytes | Native SPECLM |
| --- | --- |
| 8 | `77002a434b000000` |
| 16 | `77002a454b000000` |
| 24 | `77002a474b000000` |
| 248 | `77002a7f4b000000` |

Original evidence is in workspace build/firefox-native-speclm-strides.json
and build/firefox-eot-f{10,11,17}-c4/. Do not infer GPU correctness from an
offline byte match. The state test checks stride bounds, preservation of
unrelated bytes and forwarding to ordinary fragments, clears and EOTs.

`rgba32-attachment-probe.c` checks clear, draw and reload with exact full-width
float/signed/unsigned values. The llvmpipe reference passes all9 readbacks:
36,864 pixels /147,456 channels. Vulkan `g17p_offscreen_triangle.c` with
G17P_RGBA32_SFLOAT=1, G17P_HOST_IMAGE_COPY=1 and TEST_DEVICE_ID=0x8132
isolates rendering from GPU meta-copy.

## Pending hardware qualification

The ARM64 library builds and 6,812 Ioto checks plus14 EOT byte tests pass.
J713 became unreachable before installing the stride change on2026-09-12
at approximately03:49UTC. The current patch has **not** passed GPU pixels.
Next: guarded RGBA32 oracle, unchanged native Firefox prefix replay, fresh
Firefox WebGL capture and normal-launch validation. Also rerun the existing
mixed-width HDR oracle because it previously used the same constant stride.
