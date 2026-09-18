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
