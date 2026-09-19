# Experimental Gen9 lossless render-target compression

The pinned J713/25G83 PBE constructor at 0x33620c (UMD SHA256 in
native-descriptor.json) places compression in word0[59], extended in
word1[63], metadata address >>4 in word2[43:0]. pbe-state.py executes
that constructor offline on owned synthetic inputs; it does not execute GPU
work. The codec deliberately admits only Gen9 single-level, single-layer,
single-sample 2D descriptors and rejects unaligned/out-of-range metadata.

The matching texture reader also needs word0[59]. Its absence caused both
compressed clear/readback cases to return zero despite correct stores.
Saved UMD encodeImageReadState 0x4ad0b4..0x4ad0bc derives it from nonzero
metadata; 0x4ad614..0x4ad628 installs the metadata pointer and extension.

Allocation remains opt-in: HK_M4_COMPRESSED_2D=true, and separately
HK_M4_COMPRESSED_FEEDBACK=true for feedback-capable attachments. Only
RGBA8/BGRA8 UNORM, one mip/layer/sample, with existing mutable-format and
sparse restrictions, are candidates. Host-transfer and storage images remain
uncompressed. HK_M4_COMPRESSION_AUDIT=true identifies actual allocations.
Set ASAHI_MESA_DEBUG= explicitly for tests: the usual CTS/replay helpers
force nocompress unless overridden. Default desktop allocation is unchanged.

Validation on J713 with watchdog60 and thermal95 guards:
- Descriptor host test passes, including the native byte witness and invalid
  compressed forms. Corrected a stale negative test rejecting single-layer
  array descriptors despite the already-supported array codec.
- 20260918T173431638832Z-factorio-compression-enabled-cts: two failures,
  zero readback, before the texture-reader bit fix.
- 20260918T184721183465Z-factorio-compression-read-cts: those two pass
  with actual compressed allocations (256x256 and 200x180).
- 20260918T203811693622Z-factorio-compressed-feedback-target: four
  256x256 renders and CPU readbacks, zero differing components; allocation
  audit confirms compression, including feedback usage. Peak 60.5 C.
- 20260918T203849098312Z-factorio-compression-feedback-cts: 48 selected,
  8 Pass and 40 NotSupported, no failures; peak 61.5 C.
- 20260918T203912854810Z-factorio-compressed-frame: original retained
  Factorio frame has zero differing pixels. Its captured allocation flags
  did not select compression, so this is only an unaffected-path regression.

Earlier compressed-target and compression-cts runs did not enable actual
compression and must not be counted as compression qualification. Live menu
performance is pending; this is not a 60 FPS claim or a general compression
support claim. Array/mip/MSAA, format reinterpretation transfers, eMRT,
host-copy and sparse integration require separate qualification.

Live candidate: `20260918T203940085477Z-factorio-compressed-menu`,
11.169 FPS (10.69–11.78), peak 62.75 C, actual full-size compressed
allocations confirmed in guest stderr. No demonstrated FPS gain against the
12.954 FPS earlier dynamic-menu sample; scenes/pass counts differ. Full-size
fragment mean was 1.413 ms versus 2.062 ms earlier, but this is not a paired
fixed-scene comparison. Compression remains experimental, not default.

## Exact cross-format copies (2026-09-19)

`vkCmdCopyImage` selected the raw block-copy path whenever source/destination
formats differed. That path cannot decode lossless image compression and
rejected compressed RGBA8/BGRA8 copies at command-buffer recording. The common
render-copy shader packs the source format and unpacks the destination format,
so these UNORM layouts preserve raw bytes through native, compression-aware
views. Select that path for this supported pair when either image is compressed;
retain raw copying for the other formats and uncompressed pairs.

The standalone `vulkan-compressed-copy-probe.c` reproduces the original rejection
(`20260919T012104918240Z-compressed-rgba-bgra-copy-baseline`). With the fix,
`20260919T012246270537Z-compressed-rgba-bgra-copy-render` passes all 16 cases:
both channel-layout directions, full/offset partial rectangles, all four
compressed/uncompressed allocation combinations, and exact preserved surrounding
bytes. Allocation audit confirms actual compression. No numerical tolerance is
used. Hardware runs require the usual watchdog60/thermal95 guard.

The Factorio 85-second menu observation
`20260919T012328880318Z-factorio-compressed-copy-large-shaders` completes without
recording/GPU errors. It also captures large shaders with disk cache disabled;
its 15.78 FPS tail is diagnostic, not a production throughput comparison.
The earlier live raw-copy failure's precise formats were not retained, so this
specific reproducer/fix is not proof that every cause of that rejection is gone.
Raw-copy rejection diagnostics now retain format, compression and region details
for the next occurrence. Full CTS and broader compressed format handling remain
TODO. 60 FPS is still the performance objective.

## Compressed 2D / 3D slice copies (2026-09-19)

The live run `20260919T020839912114Z-factorio-compute-tail-menu` stalled when
its animated scene copied sixteen RGBA8 16x16 rectangles from a compressed 2D
image into consecutive slices of a 3D image. The non-2D selector chose raw
copying, which rejects compressed storage and returned
VK_ERROR_FEATURE_NOT_PRESENT at command-buffer end. This is separate from
query/timestamp execution. The preserved stderr includes the actual formats,
image types, compression flags and offsets.

Use the same exact native-format render copy for the qualified compressed
RGBA8/BGRA8 pair on M4, resolving each 3D Z slice independently of 2D array
layers. Other formats and uncompressed copies retain their existing paths.
The host slice test covers offsets, depth and signed overflow; all 3672 prior
layer cases still pass. `vulkan-compressed-volume-copy-probe.c` checks 96
same/cross-format cases, full and partial rectangles, 2D->3D and 3D->2D,
all four destination/source compression request combinations, and untouched
slices. 3D allocations stay uncompressed. Actual eligible 2D compression is
confirmed by the allocation audit. Run with ASAHI_MESA_DEBUG=,
HK_M4_COMPRESSED_2D=true, HK_M4_COMPRESSED_FEEDBACK=true and
HK_M4_COMPRESSION_AUDIT=true under gpu-experiment.py.

`20260919T021326753233Z-compressed-volume-copy-all-formats` passes all 96 cases
with zero differing bytes, peak 62.5 C. Full CTS remains deferred.

The live menu `20260919T021343773048Z-factorio-volume-copy-menu` completes its
60-second observation without recording errors or stalled FPS (21.28 FPS tail,
20.59--21.84, peak 87.75 C). This scene exercises sixteen tiny slice copies per
frame: 6460 16x16 renders in the final 19.1 seconds. Correctness is restored,
but one render command per slice has substantial firmware overhead and needs
batching. This is not a paired comparison with the earlier 39 FPS scene.

## Batch compressed-source copies through common compute

A compressed RGBA8/BGRA8 source can be sampled normally while an uncompressed
2D/3D destination accepts ordinary storage writes. Use the common Vulkan meta
compute copy for this pair on M4: native source views, exact format pack/unpack,
and bounded image stores. Consecutive small regions share a CDM instead of
requiring a tile render for each slice. Compressed destinations retain rendering.
HK_M4_COMPUTE_COMPRESSED_COPY=false is a diagnostic fallback; true is default.

`20260919T021634455712Z-compressed-volume-compute-all-formats` passes the same
96 exact-byte cases (including actual compressed sources), peak 63.5 C.
`20260919T021650522676Z-factorio-compute-copy-menu` completes the untouched
60-second live menu, tail 41.55 FPS (39.99--43.97), peak 88 C. Scene/pass counts
vary across runs, so this is not a paired FPS ratio. The final 19.07 seconds
contain 8109 renders and 6366 computes, with no tiny per-slice renders. Full CTS
and sustained 60 FPS remain pending.
