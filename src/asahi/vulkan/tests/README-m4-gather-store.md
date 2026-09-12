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
