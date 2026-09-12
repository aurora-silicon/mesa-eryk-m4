# M4 gather and storage-image oracle

`m4_vulkan_gather_store.c` uploads a deterministic 32x32 RGBA32F image,
executes `m4_vulkan_gather_store.comp`, and downloads RG16F and R8 outputs.
The shader gathers red without an offset and green with offset (1,1), then
writes two different storage images using the same coordinates. It catches
incorrect descriptor indices and premature coordinate-register discard.

Build the C harness against Vulkan and the shader with:

```
glslangValidator -V m4_vulkan_gather_store.comp -o gather-store.spv
cc m4_vulkan_gather_store.c -o gather-store -lvulkan
```

On host lavapipe, run `gather-store gather-store.spv reference.bin`.
Build the harness for the target architecture and copy the original shader
and reference bytes to the target. Run the target through the workspace
`tools/gpu-experiment.py` watchdog/thermal guard:

```
gather-store gather-store.spv output.bin reference.bin
```

The optional fourth argument requires all 5120 bytes to match; a mismatch
returns failure. Saving an output without comparison is not a pass.

M4 J713 evidence on 2026-09-12:
`20260912T065006968009Z-native-gather-coordinate-lifetime-qualified`, all 5120
bytes identical to lavapipe, peak PMP temperature 53.5 C. Shader stores use
native image-write instructions and GPU descriptor loads; transfers are GPU
copies. This qualifies the small oracle, not vkQuake or a Vulkan conformance
claim. Other image dimensions, coherent stores and additional formats remain
outside this lowering's accepted domain.

## Half-precision arithmetic

The same harness accepts `m4_vulkan_half_math.comp` when
`M4_TEST_FLOAT16=1` enables Vulkan shaderFloat16. Generate the lavapipe reference
and run the guarded target with that environment variable and the same SPIR-V.
The test covers fused and saturated half arithmetic, mixed float widths,
rounding, sign modifiers, comparisons, reciprocals and square roots. Power-of-two
inputs are formed from float bit patterns. The host LLVM22 lavapipe build
crashes in its half `exp2` path, so this oracle does not test half `exp2`/`log2`;
that reference limitation is not a target pass.

`20260912T070423896733Z-native-half-math-indexed-resources` matched all 5120
bytes exactly on J713, peak54C. The vkQuake SSAO compiler capture
`aafaef2ceef40676f38b6a30129d06b78395a511470c91cefaa3f2760d650277`
also compiles after these fixes; that is not proof of complete game execution.

## Storage reads and image visibility

`m4_vulkan_image_read_write.comp` writes RG16F, executes an image memory
barrier, reads the written texel, and uses that value in its R8 output.
Use the same harness and reference comparison. `M4_TEST_PREFILL=1` optionally
initializes the RG16F image to (0.5, 0.25) through a transfer before dispatch,
which distinguishes stale reads from forced zero results.

`20260912T071613140503Z-native-image-read-write-dependencies` matched every
byte on J713. The image-write completion token makes the texture fence wait
for outstanding writes; repeated fences without new writes add no wait.
A separate prefilled read and image-size probe passed. Before the completion
fix, write/read returned the old texels exactly while the final RG16F transfer
already contained the new values. The descriptor interpretation was correct.
The native compiler witness is retained under the workspace
`_provenance/vkquake-reference-20260912/native-image-read-write-probe/`.
