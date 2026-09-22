# Zink geometry limits and M3 PBO uploads (2026-09-22)

The ES31 campaign stopped on 63 image-comparison failures in
`texture.specification.teximage3d_pbo`. Both an array and a 3D RGBA32F case
reproduce with one worker, excluding four-worker contention as their cause.
The captured first layer is populated; the next layer is unwritten.

Honeykrisp correctly reports `geometryShader=false` on M3. Vulkan geometry
limit properties can nevertheless be nonzero when that feature is absent.
Zink copied those properties into its Gallium caps unconditionally, although
its per-stage geometry shader caps correctly indicated no support.
`st_init_pbo_helpers` uses `max_geometry_output_vertices >= 3` to select a
geometry-shader layered upload when vertex layer output is unavailable. It
therefore selected an unavailable stage. The trace shows eight-instance
layered upload draws for an 11x20x8 texture.

Only populate the Gallium geometry limits when the Vulkan geometry feature
is supported. This keeps the existing M3 feature boundary intact and makes
Mesa select its existing supported fallback transfer path. No Vulkan feature
is disabled by this fix, no CTS expectation is changed, and geometry shaders
or ES3.2 are not newly implemented. Layered PBO transfers may use CPU staging
through the existing fallback; final texture rendering remains on the GPU.

## Validation

All 63 exact failures pass with one worker. All 1236 texture specification
cases from frozen ES31 session 2 then pass with four workers, covering PBO,
non-PBO, image/subimage and unpack-parameter paths. No skips, failures,
crashes or GPU faults occurred in those focused runs. Original campaign
failures remain retained, with hash-verified passing repair evidence.

Every segment ran under the M3 45-second guard, 60-second watchdog and
five-channel 0.5-second thermal monitor with the 95 C stop. Boot remains
`1c5cdaba-93d8-4344-b10d-8a5e7c75ba9b`. This is focused regression coverage,
not a full conformance claim.

- `build/m3-gles31-20260922/triage-pbo3d/geometry-cap-fix`: 63 Pass, peak 51.25 C; results SHA256 `30c009708b990b62864bead798af54255ef8e7803d74085b00a58743c47845e0`.
- `build/m3-gles31-20260922/triage-texture-specification/00/run`: 160 Pass, peak 49.25 C; results SHA256 `0401b0542136586b2eb0d5ca60118d0522e673d6b3b71f529ad942a5d821f3a1`.
- `build/m3-gles31-20260922/triage-texture-specification/01/run`: 160 Pass, peak 49.00 C; results SHA256 `9a9035436fc7d31b7b51112d97262bfb3fea31548c18e1e1a230ceeb1a380498`.
- `build/m3-gles31-20260922/triage-texture-specification/02/run`: 160 Pass, peak 49.50 C; results SHA256 `a4aa5832cf84ec57b7121c57b3ab8ccef4923929b8a00a5cf7db7efab1048841`.
- `build/m3-gles31-20260922/triage-texture-specification/03/run`: 160 Pass, peak 50.50 C; results SHA256 `0f18dc6f68655d4d3b7be9deb039056cb1cf7c841c2efb6f97caa261c9d69819`.
- `build/m3-gles31-20260922/triage-texture-specification/04/run`: 160 Pass, peak 51.25 C; results SHA256 `3ac4fa4a27b1eda53aaff25c5ea67c79490e769c304c1af838964d8cb76c781f`.
- `build/m3-gles31-20260922/triage-texture-specification/05/run`: 160 Pass, peak 51.50 C; results SHA256 `b6205306b78893947df160ef4c03fdb3ff0ea872fe72b20a18c183606947feaa`.
- `build/m3-gles31-20260922/triage-texture-specification/06/run`: 160 Pass, peak 51.50 C; results SHA256 `2b3700afbf194154f46e60a5bb46e04e11bd5027c8328eafb84ed580a1754082`.
- `build/m3-gles31-20260922/triage-texture-specification/07/run`: 116 Pass, peak 51.50 C; results SHA256 `88f4d94d0f435233524bbe901cdfd4ba9505ca69df43b2e76f870836048e3fae`.
