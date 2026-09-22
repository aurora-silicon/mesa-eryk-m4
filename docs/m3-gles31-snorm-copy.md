# M3 exact SNORM image copies (2026-09-22)

The ES31 copy-image case
`dEQP-GLES31.functional.copy_image.non_compressed.viewclass_32_bits.rgba8_snorm_r32i.texture2d_to_renderbuffer`
failed image verification (catalog index 100730). The serial reproducer fails
on the same boot with a healthy guard and GPU. Destination differences of
16777216 and 16777472 correspond to changed high-channel bytes in the R32I
reinterpretation; a one-byte error is not a permissible integer tolerance.

Intermediate same-format SNORM copies used the render-based meta path. SNORM
has two representations of -1 (for eight-bit values, -128 and -127). Sampling
and repacking canonically stores -127, losing the original bits even though
a normalized source-image comparison still passes. A later integer-view copy
exposes the changed bytes. The copy trace records repeated
R8G8B8A8_SNORM-to-R8G8B8A8_SNORM operations before the final reinterpretation.

Route uncompressed eight-/sixteen-bit SNORM formats through the existing
native raw-copy path for buffer uploads and image copies. This also covers
texture reallocation copies, without floating-point conversions, shader
precision changes or altered test expectations. Extend the existing optional
copy trace to report image-copy formats and compression state. Diagnostic
logging remains disabled by default.

Baseline evidence: `build/m3-gles31-20260922/triage-snorm-copy/serial`.
The first candidate passes the exact case under `candidate`, with its optional
copy trace enabled. Wider validation uses `neighbors-*/run` with the trace
disabled. All runs retain the nonrenewing 45-second M3 guard, 60-second
watchdog and five PMP channels at 0.5-second cadence with a 95 C stop.
The desktop continues using its validated production build during these tests.

## Completed validation

All 1080 selected SNORM copy cases pass, across 34 guarded segments using four
workers (32 cases per segment, eight per worker group). There are zero skips,
failures, crashes or GPU faults. Peak monitored temperature: 54.25 C.
These GLES cases cover eight-bit SNORM; they do not separately qualify every
sixteen-bit Vulkan format. The selected catalog indices are unchanged.

`validation-manifest.json` in the evidence directory records each segment's
result hash; manifest SHA256 `a7af2ee600a24acde386db25f1360776c9963e3a4910ebfe24240eb8916256d3`.
The kernel and desktop session stayed on boot
`d203b3d5-8586-4e59-9820-848216771e86` throughout the baseline and regression.
