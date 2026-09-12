# Native raw image transfers

`m4_vulkan_raw_upload.c` checks GPU upload/readback of R8, RGBA8, R16F,
RGBA16F, R11G11B10F, RGBA32F, D32F, and both separate D32F/S8 aspects.
Each checks two mip levels, two array layers, full uploads, 128 small padded
patches, mixed-mip regions and a layered patch. The CPU builds inputs and
compares bytes; compute shaders perform transfers. Build with `-lvulkan`.
Run host lavapipe with `--reference`; J713 requires gpu-experiment.py and the
normal experimental ICD/nocompress environment.

2026-09-12:

- Software reference: all nine cases byte-exact.
- 20260912T053925474186Z-native-raw-upload-formats: initial upload extension
  passes float/depth; packed-float formatted readback changes 6,339 bytes and
  stencil compilation rejects odd-half scalar byte loads.
- 20260912T054144514338Z-native-raw-transfer-formats: raw compute readback plus
  scalar byte load operand admission passes all nine cases; peak 53.75 C.
- Host AGX3PackBinary.ScalarByteLoadWritesOddNativeHalf passes.
- 20260912T054216759068Z-stk-race-raw-transfer-fixed: retained 11-frame race
  replay completes without its previous rejected HDR/depth uploads. Frame10
  mean channel error falls from 52 to 0.276; remaining >8/255 differences
  occupy 11,967 of 1,946,984 pixels, mostly foliage/edges. This is an explicitly
  adapted trimmed replay, not original-application qualification.

The raw path uses layout element geometry and separate depth/stencil planes.
It does not implement packed combined depth/stencil masked copies, compressed
render-target layouts, multisampled or 3D transfers. Existing qualified paths
remain selected for their supported upload shapes. No application features
are silently removed by the driver.
