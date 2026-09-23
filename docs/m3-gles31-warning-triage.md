# M3 GLES 3.1 campaign warning triage

This is triage of retained warnings, not a conformance claim or a driver fix.
The current path is Zink over Honeykrisp on the M3 Pro. The pinned CTS source
is `f6a29701220f34dd1407513bfe80d74ca7b392ce` (1.4.6.2). Original QPA files
remain under `build/m3-gles31-20260922/campaign/segments` in the host workspace.

## Optional framebuffer completeness: three warnings

The GLES2 and GLES3
`functional.fbo.completeness.renderable.texture.color0.srgb_alpha_unsigned_byte`
cases create a 64x64 `GL_SRGB_ALPHA` / `GL_UNSIGNED_BYTE` texture. Both QPAs
say that color renderability is optional and accept
`GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT`, which is what the implementation
returns. GLES2 also permits `GL_FRAMEBUFFER_UNSUPPORTED`; both permit
`GL_FRAMEBUFFER_COMPLETE`. Evidence: segments 0069/c6.r1.qpa and
0210/c8.r1.qpa, each inside `run/deqp-runner`.

`dEQP-GLES3.functional.fbo.completeness.samples.rbo1_rbo2_rbo4` requests
RGBA8, RGB565 and DEPTH_COMPONENT24 renderbuffers with 1, 2 and 4 samples.
The QPA accepts either `GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE` or complete;
the implementation returns the former. Evidence: segment 0211/c3.r1.qpa.

`modules/glshared/glsFboCompletenessTests.cpp` emits a QualityWarning when
an accepted incomplete result is returned while complete was also allowed.
These three results do not establish an invalid framebuffer status. Retain
them as warnings, not passes; do not change CTS expectations or pretend that
mixed-sample rendering was qualified.

## Line interpolation: eight quality warnings

The GLES2/GLES3 basic/projected line cases, plus GLES3 single-sample FBO line
cases, match the CTS verifier's major-axis projected interpolation model.
The verifier explicitly distinguishes this from its strictly correct model
and emits QualityWarning. Evidence: segments 0074 and 0245. See
`modules/gles3/functional/es3fRasterizationTests.cpp`, the
`LINEINTERPOLATION_PROJECTED` branch (and its GLES2 counterpart).

This is a rendering quality limitation. It is not justified to call the
reference wrong. No correction has been qualified for these warnings.

## Multisampled wide lines: thirteen compatibility warnings

The retained GLES3 results cover primitive and interpolation checks on wide
lines in multisampled FBO/default configurations (segments 0245 and 0280).
The pinned verifier deliberately returns CompatibilityWarning for incorrect
wide-line results when sample count is greater than one; comparable narrow
or single-sample errors would instead fail.

The current Vulkan driver advertises Bresenham lines but neither rectangular
nor smooth lines; Zink falls back from unsupported rectangular modes to the
default Vulkan mode. This identifies a relevant path to investigate, not a
proven root cause or a reason to relabel the observed warnings as passes.
Neither a capability expansion nor a rasterizer change was made for this
triage.

## Boundaries

The four earlier acos precision warnings are separate: their qualified fix
and passing retests are documented in `m3-gles31-acos-precision.md`.
Timeouts, interrupted tests, missing capabilities and warning results remain
separate from passing tests. Mixed-build campaign coverage plus focused
repairs is not a complete final-build conformance submission.
