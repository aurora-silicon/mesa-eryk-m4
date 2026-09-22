# M3 attachmentless multisampling (2026-09-22)

The ES31 GL_MAX_FRAMEBUFFER_SAMPLES queries returned zero: Honeykrisp exposed
only VK_SAMPLE_COUNT_1_BIT for framebufferNoAttachmentsSampleCounts even
though native G15S can render attachmentless multisampled framebuffers.
Use the same existing G15/G16 1x/2x/4x sample-count capability as color/depth
rendering. This enables ES31 functionality, without adding ES32 features.

Validation includes actual rasterization checked by occlusion queries, not
just allocation or limits. All 72 no-attachment CTS cases and the four exact
limit queries pass on J514S. A second isolated run of the maximum-size/sample
case passes. The maximum framebuffer remains 16384x16384 with four samples.

The maximum-size, four-sample case exposed a separate kernel stall with the
old fixed 64 MiB parameter pool. The validated kernel provides 256 MiB and
updates the native PB page/block lists and all firmware/hardware capacities.
Deploy this capability together with that kernel fix. Full-size single-sample
rendering passed on the old pool, but the combined maximum did not.
See kernel m3_lab/GLES31-PARAMETER-POOL.md for the fault and recovery evidence.

The final kernel module SHA256 is
`f276b11ab10fc52e655e953b1acee28652b07799c0feb2a523e6f8791329a0a7`.
Each run retained the 45-second nonrenewing guard, 60-second watchdog, five
PMP channels sampled at 0.5 seconds and a 95 C stop. Earlier failures remain
in the evidence; this is targeted coverage, not complete GLES conformance.

- `exact/final`: 4 Pass, peak 50.50 C, results SHA256 `fd73ce4721d451339f254da55b7b31058a15fdb795b6c0a622bddb2db3da31ac`.
- `maximum-all/final`: 1 Pass, peak 52.50 C, results SHA256 `c6d082ee2bbc2deb400c5c85ee752e53695a79b41312c72065dd1a16e378cfc0`.

The 72 cases cover every attachmentless rendering case, including all five
maximums; four exact capability queries also pass. The former faulting case
passes again in a fresh client process. No GPU fault, reboot or guard failure
occurred in the final runs. Plasma startup selected the native M3 render node.

