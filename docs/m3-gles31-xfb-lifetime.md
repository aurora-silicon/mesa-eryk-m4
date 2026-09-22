# Preserve transform-feedback metadata across native shader cloning

The GLES31 campaign stopped in segment 490 with six copy-texture conversion
failures. The first isolated failure was
`KHR-GLES3.copy_tex_image_conversions.required.cubemap_negx_cubemap_negx`
(global index 121285, EGL config 1). Renderable-format checks succeeded, but
non-renderable formats use a vertex shader and transform feedback to compare
the source and destination textures. All three feedback buffers remained zero,
including the comparison-result buffer.

`hk_compile_nir` received `xfb_info` belonging to its input NIR shader. Native
sampler lowering cloned the shader and freed that input. At the end of
compilation it copied the now-dangling feedback metadata. In the captured run
no passthrough geometry shader was compiled to implement feedback. Preserve
metadata and strides in `shader->info` before any input-shader replacement.
This fixes the ownership error without changing shader code, copy semantics,
feature advertisement, or CTS expectations.

Qualification:

- Fresh-process normal-build baseline fails, including with shader dumps.
- The unchanged case passes in all 15 selected EGL configurations after the fix.
- Full conversion/feedback neighboring validation is pending.

Evidence: `build/m3-gles31-20260922/triage-copy-convert-{baseline,debug}` and
`triage-xfb-candidate`. The candidate is `/opt/asahi-m3-cts-xfb-lifetime`.
Tests retain 20-second case, 45-second experiment, 60-second watchdog and 95 C
thermal bounds. Debug shader dumps are separate from normal validation runs.
