# Capture/replay buffer image coherence (M4, 2026-09-25)

Satisfactory DX11 through DXVK could show its menu without recording, but its
first luminance reduction hung when GFXReconstruct enabled replayable device
addresses. The original SPIR-V and compiler input passed in isolation until
the oracle also requested DEVICE_ADDRESS_CAPTURE_REPLAY buffer/allocation
flags. That single condition reproduced the timeout.

Capture/replay reserves a buffer VA that aliases its backing allocation.
Honeykrisp already canonicalizes global accesses (including image atomics)
through the alias table. Buffer texture/PBE descriptors still addressed the
alias, and texture reads additionally used the read-only shadow. Mixing these
addresses in one image reduction did not preserve visibility: the CAS lock
could remain set after the image-store release, stalling all later workgroups.
Replacing the release with an atomic exchange prevented the hang but still
produced an incorrect maximum. It is a diagnostic, not the fix.

Remember the backing address at BindBufferMemory2 and use it for both sampled
and storage views of non-sparse buffers on native G15/G16. Preserve the public
replayable device address and all sparse per-page addressing. Ordinary buffers
without replay aliases retain their existing descriptor address.

Validation:
- Original shader, captured 512x512 RGBA16F input and full game features.
- GPU clear, compute statistics query, 65536-byte R32UI view at buffer offset
  267911168, replayable buffer address enabled.
- Before: GPU timeout; after: 100/100 exact full-buffer comparisons.
- Correct result: coords (255,256), luminance bits 0x45779244, lock zero.
- All runs guarded by 60-second watchdog and all-channel temperature monitor;
  fixed-run peak 69.75 C.
- Host sparse/alias lowering test passes.

Evidence: dirtyroom_m4/_provenance/experiments/
20260925T015148805204Z-satisfactory-capture-address-reduction and
20260925T015313856192Z-satisfactory-capture-address-canonical-view.
Full startup replay `20260925T015340982759Z-satisfactory-full-canonical-views`
completed all 1100 frames with original shaders and no feature removal or
memory rebind. A descriptor dump verified the formerly hanging dispatch's
exact result (255,256,0x45779244,0). Peak temperature was 95 C. Its splash frame
is regression evidence, not a menu/gameplay benchmark. Live scene recording
is a separate qualification. M3 hardware was not touched.

## Compiler capture diagnostic

To isolate a known program in a large replay, combine
`AGX_COMPILER_CAPTURE_DIR=/path/to/captures`,
`AGX_COMPILER_CAPTURE_SUCCESS=compute` and
`AGX_COMPILER_CAPTURE_BINARY_SIZE=5734`. Zero (the default) disables the size
filter; it filters only successful captures and does not change compilation.
The full replay and passing isolated program produced identical 7576-byte
serialized inputs, BLAKE3
`583a0720274205569d21316c4f95df9ed60d70da8ceb086193370c54703f0460`.
Their equality ruled out a different compiled variant before isolating the
capture/replay address flag.
