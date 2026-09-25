# One-layer D3D texture-view aliases (M4, 2026-09-25)

Satisfactory DX12 final tonemap (draw 1010365, PS 11474) reads a non-array
SPIR-V 2D image through a 2D_ARRAY view with one layer. Descriptor copy tracking
and readback show layer 3 of image 11356 has the correctly lit TSR result; the
non-array sample returned near zero. An array shader with explicit layer zero
reads it correctly. Globally changing all one-layer array descriptors to 2D
breaks other rendering and was rejected.

Keep both native descriptor shapes, including the original base layer, format,
swizzle and mip range. The sampled software tail now has an array heap index at
byte 76 (80-byte Gen9 stride); non-array and buffer instructions retain index
68. Input attachments keep their own array descriptor. Storage and buffer tails
are unchanged. Image-view destruction and allocation-failure cleanup release
the extra descriptor.

This is D3D compatibility, not a claim that the old behavior violated Vulkan.
VKD3D af89350cc2eacd9 documents D3D's Texture2D/Texture2DArray reinterpretation
in vkd3d_texture_view_desc_fixup. The capture does not set the maintenance11
single-layer-alias image flag. This change does not advertise maintenance11.
Source: https://github.com/HansKristian-Work/vkd3d-proton/blob/af89350cc2eacd9/libs/vkd3d/resource.c

## Standalone reproduction

Build the source and both shaders (no game data):

```
glslangValidator -V vulkan-single-layer-alias.comp -o plain.spv
glslangValidator -V -DARRAY vulkan-single-layer-alias.comp -o array.spv
cc -O2 -Wall -Wextra vulkan-single-layer-alias.c -o alias-probe -lvulkan
```

Run `alias-probe plain.spv array.spv` under the mandatory GPU guard to check
Vulkan-valid plain and array views. Add `--compat` to explicitly test the D3D
alias beyond Vulkan's requirements without maintenance11. Both textureLod and
texelFetch are checked across four mip levels and all four base array layers.
Before: valid cases pass, compatibility cases fail 1024 scalar comparisons.
After: all 3072 scalar comparisons pass. This is not a CTS replacement.

Zink regression: build array-lod-probe.c with -lEGL -lGL; set
M4_TEST_IMAGE_WRITES=1 and M4_TEST_VIEW_LAYER=0..3. All compute-written mip
levels and array reads pass exactly before and after.

Guarded evidence IDs in the local benchmark repository:

- 20260925T093236727215Z-single-layer-view-baseline
- 20260925T093526094875Z-single-layer-view-alias
- 20260925T093838386535Z-single-layer-native-alias-matrix
- 20260925T093908351241Z-single-layer-native-alias-before
- 20260925T093552583346Z-satisfactory-dx12-array-alias-menu

Original DX12 31-frame replay completes without replacements/rebinding; final
4K screenshot shows the lit factory with UI. Peak monitored temperature 107.25 C.
Native Vulkan's separate dark-lighting problem remains (093919319726Z).
Host native-descriptor and descriptor-root tests pass. No M3 execution claim.
Tested DSO SHA256: 17327a953baefafca5e35f6931c27d0c273d49ea9d4d555558f4c9c785ddb1be.
Live application qualification follows separately.

Live DX12 gameplay validation completed in
20260925T095346452539Z-satisfactory-dx12-alias-live-gameplay-warm2. Actual
Rocky Desert save at original Ultra/custom settings, 3840x2160 output and
TSR75%. Terrain, clouds, creatures and HUD render correctly after dismissing
the welcome overlay. The process loaded the hash recorded above. The guarded
run completed with 500 thermal samples, peak107.75C, no new GPU fault.
Two earlier launches stopped at the mandatory GPU-idle gate during PSO
compilation; the third completed using the accumulated cache. The earlier
replay qualification is therefore backed by an actual gameplay run.
