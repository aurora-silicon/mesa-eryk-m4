# M4 VDM batching comparison

Build `m4_gl_conditional_mipmap.c` with `cc -O2 -Wall -Wextra ... -lEGL -lGL`.
Live J713 runs require the workspace watchdog and thermal guards.

Set `M4_TEST_DRAWS=4`, `M4_TEST_MODES=2`, `M4_TEST_WIDTH=1024`,
`M4_TEST_READS=2`, `M4_TEST_CLEAR_FINISH=1`, and `M4_TEST_FINISH=1`.
Compare `M4_TEST_SPLIT_DRAWS=0` against `1`. The shader, quad, textures, draw
order, and scissor stripes remain identical. Only the intermediate `glFinish`
changes. Even stripes sample the blue mip; odd stripes sample grey. This checks
descriptor/scissor transitions, not a render-to-texture dependency.

The target probe wrapper defaults to `ZINK_DEBUG=sync`, which inserts Vulkan
barriers before draws and causes Honeykrisp to end the render stream. Override
it with an explicit empty `ZINK_DEBUG=` for **both** variants. Capture
`HK_G17P_LAUNCH_TRACE=1` and decode the actual VDM bytes before identifying a
case as batched. API call counts alone are insufficient. Keep shader and
submission debug options identical between the pair.

`HK_M4_VDM_DRAW_BARRIER=1` enables an optional G16G diagnostic. It inserts
the native-witnessed `0x21000960` packet before each subsequent draw's state
within a stream. It leaves the incomplete HAL200 bind tail contiguous with
the draw selector that completes it. This is off by default; packet provenance
does not establish which hazards it orders, and these results do not justify
permanent barriers between every draw.

The software control uses `LIBGL_ALWAYS_SOFTWARE=true M4_TEST_LLVMPIPE=1`.
Both 1024-square variants passed every pixel under llvmpipe.

## J713 observations, 2026-09-10 UTC

Kernel 70d02689cd84; Mesa diagnostic build
`a4c6a4217c6df4e4a9cbd608fde9c5029e7c859cf0c38f19169d9e1c86c0238f`.
Every live test used the 60-second watchdog and six-channel thermal guard,
with unchanged GPU clocks.

- 21:57:01 small batched: four draws, one initial barrier; all pixels correct.
- 21:57:24 large batched: four draws, one initial barrier; fragment timeout.
- 21:58:34 small with diagnostic per-draw barriers: four draws, four barriers;
  cold mode-0 first read stale, second correct; mode-2 both reads correct.
- 21:58:54 large with diagnostic per-draw barriers: four draws, four barriers;
  fragment timeout. Tiling retired. The barrier did not fix this workload.
- 22:00:25 large split, with the same empty Zink debug setting: separate
  single-draw streams, no timeout; 86,448/1,048,576 pixels stale on first read,
  zero incorrect on second read. This is **not** an overall correctness pass.

The paired runs peaked at 50.5–51.5 C on the monitored channels. The workspace
`_provenance/experiments/` retains exact commands, source diffs, target hashes,
VDM bytes, pixel checks, faults and thermal samples under the timestamped
`vdm-pair-*` experiment names.

This does not rule out other ordering hazards. In particular, splitting also
reduces fragment work per render submission. An earlier full-screen single-draw
test likewise faults, so the paired result alone does not establish batching
as the cause. Do not discard the cold-readback failure or count the later good
read as a successful first read.


## Coverage and work controls

`M4_TEST_SCISSOR_X/Y/WIDTH/HEIGHT` restrict coverage within the unchanged
square attachment. Width/height default to attachment width; X/Y default to
zero. Coordinates must fit, and scissor height must divide the draw count.
The CPU reference also checks untouched magenta pixels outside the rectangle.
`M4_TEST_ALTERNATE_TEXTURE=0` keeps the blue texture across every stripe.
These controls permit geometry/work/state comparisons without editing shaders.

The 2026-09-10 22:05–22:07 UTC J713 tests on kernel70d02689cd84 showed exact
first and second reads for all of: loop20 in 512-square origin and far-corner
regions, loop20 in a far-edge 1024x256 band, full-area direct sample, and
full-area loop5. All used a1024-square attachment, decoded submission tracing,
watchdog/thermal guards, and unchanged clocks; monitored peak50.5C. These
controls weaken geometry-only explanations. Full-area loop20 still needs a
correct execution path; reduced coverage is not a game fix.
