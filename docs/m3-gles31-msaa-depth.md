# M3 multisampled depth/stencil sampling (2026-09-22)

Four GLES3 framebuffer-invalidation cases lost the preserved multisampled
depth attachment when blitting to the default framebuffer. Their result was
solid cyan instead of the expected green/cyan depth boundary. The serial
baseline logs `ZINK: blit unsupported z24_unorm_s8_uint -> z24_unorm_s8_uint`.

Honeykrisp advertised 2x/4x depth and stencil attachments and image format
support, but its global sampled depth/stencil limits remained single-sample
on G15S. Zink therefore rejected the sampler-based blit before submitting it.
Report the existing 1x/2x/4x native sampling support for the G15/G16 shader ABI.
Other integer, storage and no-attachment sample limits are unchanged.

All four exact failures now pass. The broader 866-case GLES3 framebuffer
blit/invalidation/MSAA/depth/stencil selection produces 832 Pass and 34
NotSupported, with no failures or GPU faults. This qualifies the exercised
paths, not full Vulkan or GLES conformance. Raw campaign failures remain
preserved with separately hash-verified repair evidence.

Tests used private headless windows, the normal 45-second M3 experiment
guard, a verified 60-second watchdog, and all five thermal channels every
0.5 seconds with a 95 C stop. No reboot was needed.

## Evidence

- `build/m3-gles31-20260922/triage-msaa-invalidate/depth-sampling`: {'Pass': 4}, peak 46.25 C; results SHA256 `b94bd260d4f428a85fba595a80a2c756e2c2d4bf060b8cdcbb69f12b28a4d54c`.
- `build/m3-gles31-20260922/triage-msaa-neighbors/00/run`: {'Pass': 128}, peak 46.50 C; results SHA256 `8ea32bf6e01328e9679ec74490a820a404c570df4fee69dc7584dc5553ead23e`.
- `build/m3-gles31-20260922/triage-msaa-neighbors/01/run`: {'Pass': 128}, peak 48.75 C; results SHA256 `740d1c00f3ed9b098548b96c4e47e9cb3bd7cf8e7a2e3c55993965e09942c592`.
- `build/m3-gles31-20260922/triage-msaa-neighbors/02/run`: {'Pass': 128}, peak 49.75 C; results SHA256 `1d2bd1b77ec337cf8c4653174000553f5abf4cc6caa3cba0b38196a484ab1fc8`.
- `build/m3-gles31-20260922/triage-msaa-neighbors/03/run`: {'Pass': 128}, peak 48.25 C; results SHA256 `df00685d8d452a53776c94a0018865c96a552306446bc018e90a04fddcee69e1`.
- `build/m3-gles31-20260922/triage-msaa-neighbors/04/run`: {'Pass': 116, 'NotSupported': 12}, peak 48.25 C; results SHA256 `b0206d5dccbb1e596f0bf7593f91adc974f7fa242b16e2e64d1f2a4b1bae4030`.
- `build/m3-gles31-20260922/triage-msaa-neighbors/05/run`: {'Pass': 106, 'NotSupported': 22}, peak 49.50 C; results SHA256 `7e18e952c6881111f3d59c4bd0f92a9a32d794270fdb96d5db2accee5f01bffc`.
- `build/m3-gles31-20260922/triage-msaa-neighbors/06/run`: {'Pass': 98}, peak 49.00 C; results SHA256 `77794c3d4e6bc15a30f5d1e500d3bfa5be517f81e09677fbe3a91903606a8e18`.
