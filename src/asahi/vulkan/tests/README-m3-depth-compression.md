# M3 opt-in D32 compression

`HK_M3_COMPRESSED_DEPTH=true` enables native compression on G15S for D32_FLOAT
images with one 2D mip, one array layer and one sample. Existing sparse,
mutable-format, storage, host-transfer and feedback-loop restrictions still
apply. The color-mipmap experiment does not widen this depth scope. This is
experimental and disabled by default; it does not qualify other depth formats,
multisample/layered compression or format aliases.

The kernel must carry the existing validated depth compression UAPI fields to
the M3 ZLS metadata registers and their spill mirrors. The native macOS 26A428
command snapshots witness those fields. This preserves GPUVM ownership checks;
stencil compression remains rejected by the M3 kernel.

Buffer uploads write `gl_FragDepth` through a native depth attachment instead
of reinterpreting compressed storage as R32_UINT. The shader loads the D32 word
directly, without float arithmetic. Row padding and buffer offsets are retained;
helpers use bounded source addresses, and partial updates load the existing
depth attachment. D32-to-D32 image copies use an unscaled nearest fetch through
the native depth views. Transfer clears use the existing compression-aware
`libagx_fast_clear` kernel. Readback uses the existing sampled-image path.

This driver does not advertise `VK_EXT_depth_range_unrestricted`. The Vulkan
[buffer-to-image copy rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBufferToImage.html)
leave out-of-range depth upload values undefined without that extension.

## Evidence, October 2, 2026

The unchanged Minecraft Vulkan capture is SHA256
`edaf7119ebfbcaa4b3421f04b6d8b39f2d0e62cecf41d87d6b8f294e10872c5b`,
3024x1837, 30 completed frames. A private build with depth compression gave
105.695 versus 114.524 median replay FPS in three alternating timing pairs
(+8.35%). All three saved frames are pixel-identical to the reference. This is
replay throughput, not live Minecraft FPS, and setup uploads are outside the
timed interval. The later image-to-image-copy addition is qualified separately.

Eight CTS buffer-upload/readback cases pass with compression allocation audited,
including nonzero offsets. A subsequent 21-case pinned Vulkan 1.4.6.2 subset
passes uploads, readback, D32-to-D32 image copies across all four general/optimal
layout combinations, and single-layer attachment/transfer clears including
non-square extents. Four separately attempted depth-blit cases remain
NotSupported because the driver does not advertise a depth blit destination.
They are not counted as passes. Every case retains its frozen global index.

Generated evidence is outside Git in the benchmark workspace:
`diagnostics/m3-macos-driver-gap-20261002/{depth-upload-pairs,depth-cts-uploads,depth-cts-copies}`.
Hardware runs use the 60-second watchdog and thermal guard. Desktop Mesa
selection is unchanged. Wider alias, partial-copy and application qualification
is required before enabling this by default.
