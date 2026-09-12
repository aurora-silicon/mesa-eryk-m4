# Firefox / Zink bring-up evidence

Firefox 155.0.1 uses the normal private Mesa/Zink installation on J713. An
isolated Marionette profile supplies graphics diagnostics; native scanout is
used for images (ordinary WebDriver screenshots may use software rendering).
Automation flags are not normal browser-launch settings.

## Native varying size

The former 28-word guard rejected Firefox WebRender shaders with 46 user
words. Apple applegpu-nt targeting G16G accepts 29 float4 user locations. Its
vertex __GPU_LD_MD FlatBuffer root.field1.field3 is 120: four position words
plus 116 user words, using the existing all-f32 UVS layout. No new encoding or
layout bypass is required. The declared Vulkan component limit was already116.

Reproduce from `firefox-varyings.metal` using `native-metal-compile.py --stage
render --function fs --output NEW`. Inspect the vertex __GPU_LD_MD with
`flatbuffer-field.py FILE --path 1.3 --type u32`. Paths are zero-based.
The original archive, tools/commands and hashes are retained in workspace
`build/firefox-native-varyings116/`; vertex metadata SHA256:
`5dfc7ee9bae13911f689e108ceb7c6782cbfb4e4d0f4902fed04792d1c8bbd9f`.
The decode test covers 120 words and rejects exceeding the component bound.
Offline native compilation and packet tests alone are not hardware validation.

## Capture boundaries

- `20260912T031605707425Z-firefox-mdn-reference`: 117-frame,23MiB software
  reference; MDN WebGL textured cube visibly correct. Graphics reports Zink /
  llvmpipe WebRender. No M4 rendering claim for this reference.
- `20260912T031855463816Z-firefox-mdn-native-prefix`: reference replay rejected
  ShaderLayer even though recorded Vulkan1.2 features are false. Preserve the
  mismatch; do not strip the capability or treat the profile as equivalence.
- Native layer/viewport outputs are now correctly unadvertised; neither the
  UVS special-word layout nor layered framebuffer rendering is implemented.

All hardware runs use the 60-second watchdog and fresh six-channel thermals.

## Captured BDA buffer binding

`vk-capture-buffer-bind.c` reproduces Firefox's 1MiB buffer suballocated at
1MiB into a2MiB memory allocation, with capture/replay enabled. Before the fix,
`hk_BindBufferMemory2` includes the extra virtual guard in the BO mapping and
returns VK_ERROR_UNKNOWN. Binding only the page-rounded buffer storage passes.
Evidence: `20260912T032338882082Z-capture-buffer-bind-before` fails;
`20260912T032407605031Z-capture-buffer-bind-after` passes. This tests binding
and address acquisition, not shader execution.
