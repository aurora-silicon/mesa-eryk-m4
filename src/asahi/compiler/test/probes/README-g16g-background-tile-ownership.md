# G16G background tile stores need the fragment colour ownership protocol

`dEQP-VK.api.image_clearing.{core,dedicated_allocation}.clear_color_attachment.
multiple_layers.r32g32b32{,a32}_{sfloat,sint,uint}_{64x11,200x180}_sample_count_4`
(24 cases) returned the load-op clear colour with zero B/A in the layers the
in-pass `vkCmdClearAttachments` did not touch: `Ref:(0.2, 0.1, 0.7, 0.8)
Color:(0.2, 0.1, 0, 0)`.

## Reproduction

`g16g-layered-ms-clear-probe.c` (also `tools/layered-ms-clear-probe.c`)
mirrors the CTS pass: a layered 4x attachment view is load-op cleared, a
layer range is cleared by `vkCmdClearAttachments`, the pass resolves into a
1x image of the same layering and every view layer is read back. With
argument 11 it places the multisample image in host-visible memory and prints
the raw 16-byte texel classes and a 4 KiB block map. Argument 10 selects a
post-pass `vkCmdResolveImage`; argument 12 pre-clears through the transfer
path and uses `LOAD_OP_LOAD`. Build with `cc -O2 -lvulkan -lm`; on the M4 run
only through `gpu-experiment.py` and `run-m4-gl.py`.

Evidence (experiments `20260915T18..Z-claude-lmsclear1` .. `lmsclear14`):

- Only 16-byte samples at 4x (64 bytes per pixel) fail: RG32F, RGBA16F, 1x
  and 2x RGBA32F pass in every shape. Forcing 16x16 utiles for RG32F 4x and
  2x RGBA32F passes; giving RGBA32F 4x a 32x16 utile (32 KiB) still fails.
- Tiles a draw touched are always correct, at any offset. Empty tiles lose
  the upper 64 bits of every sample once the pass has processed roughly
  1 MiB of tile storage (256x256 4x loses everything after the first 64
  16x16 tiles; 64x16 x 16 layers loses layers 8-11). The raw memory shows
  the EOT wrote the full 16 bytes for the first tiles and 8 bytes after.
- The layered MS EOT is byte-identical to the AIR-NT 32023.917 type8 RGBA32F
  builtin (`native-eot-input.py 17:4 --multisample-array`, byte-identical to
  the RG32F one; only the IMGWBLK selector differs from RGBA16F). The clear
  BG differs from the RG32F clear BG in the sample mask byte only. Wider
  BG Ioto release masks (0x2f3) and an inline `pixrel` alone change nothing.
- Emitting the ordinary fragment colour ownership protocol in the BG shader,
  `87 02 54 00 06 00` (entry gate), `87 02 54 0c 08 00` (colour writeout
  wait) before LSTCOL and `07 02 54 0c 02 00` after it, keeps every sample
  in all probe shapes, including the forced empty-tile reload pass that
  previously lost isolated samples at the 1 MiB boundary.

## Change

AGX3b fragment shaders compiled with `ignore_tib_dependencies` (the native
clear and reload background programs) now emit the entry gate once, the
writeout wait before each tile store and the release after the last store,
exactly as the linked fragment epilog does. The BG Ioto keeps its witnessed
`0x200` terminal release. Compute EOT programs are unchanged.

Hardware: the 24 CTS cases pass (`claude-lclear-run1`), reg1000 is 815 Pass /
186 NotSupported (`claude-reg1000-bgproto`), and 1,500 previously passing
image_clearing, multisample, renderpass and dynamic_rendering cases stay at
1,491 Pass / 9 NotSupported (`claude-bgproto-reg1`). Apple's own tile
background program was not captured; this protocol is grounded on the
fragment epilog witnesses and the hardware result above.
