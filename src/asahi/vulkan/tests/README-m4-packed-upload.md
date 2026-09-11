# Native packed texel uploads

`m4_zink_packed_upload.c` reproduces Sauerbraten's GL_RGB5_A1 upload path using
BGRA / UNSIGNED_SHORT_1_5_5_5_REV data. It tests all 32 channel values and both
alpha values, seven rectangular mip levels including one-pixel tails, mip-chain
migration, and an interior patch with padded rows and pixel/row skips. Two passes
sample 5462 pixels into RGBA8 with an analytic reference (one channel-level
rounding tolerance). The tested path performs GPU uploads/copies/sampling;
the CPU constructs the inputs and checks the outputs.

Build with `-lEGL -lGL`. Host llvmpipe uses `M4_TEST_LLVMPIPE=1`; hardware requires
the existing J713 watchdog/thermal experiment guard and `/root/run-m4-gl.py`.

2026-09-11 evidence:

- 141349 installed qualified Mesa baseline: native upload admission rejects
  VK_FORMAT_A1R5G5B5_UNORM_PACK16; test fails as expected, peak50.25C.
- 141445 admission-only candidate: still fails because F16 tile-store EOT is
  not encoded. That candidate is discarded.
- 141811 raw compute copy candidate: all5462 pixels pass, peak49C, no GPU fault.
  Host llvmpipe also passes. The raw copy uses 16-bit loads/stores and the image
  layout's element geometry, preserving packed bits through upload/mip copies.

This qualifies the packed texture transfer path, not rendering directly into
packed attachments. The pre-existing native query-helper checkpoint is in this
candidate build; timestamp GPU-reset ordering remains unqualified.

141902 BC regression passes all1,310,720 pixels across BC1/2/3 UNORM/sRGB,
ten mips and interior updates, peak49.25C. Existing region/layer admission host
fixture passes3672 cases. ARM64 Mesa build passed. Mesa remains local-only.
