# Fixed Linux 100-case development target

User requested 100 Linux passes on 2026-09-13, explicitly excluding Windows.
Retain the applicable original sample and its order. Replace six Windows-only
cases and four requests for DISJOINT on single-plane formats with ten samples
from the original mustpass population (seed 20260918). No exclusions depend on
whether a test passes or on optional device feature support. Original case 89
is valid and retained. Exact rules, removals and additions: selection.json.

Use the separately identified CTS 1.0.2.6 diagnostic binary with four retained
corrections: input-aspect lifetime/subpass reference, YCbCr readback layout,
scaled clear union member, and the invalid destination-linear-filter precheck.
The patches preserve pixel comparisons. Source and original executable were
restored after building. Exact patch and binary hashes: cts-build-manifest.json.
This is a development target, not an unmodified CTS or Vulkan conformance claim.
The original 100-case report remains separate in ../cts-1.0-baseline/.

Initial run: 81 Pass, 19 NotSupported, no Fail or Crash. All 100 outcomes and
guarded evidence paths are retained in initial-results.json and
initial-summary.json. Every hardware batch uses the 60-second watchdog and
0.5-second thermal guard. Never replace a failing or unsupported Linux case to
improve this target's result.

Target files: /root/cts-1.0.2.6/linux-cases.txt and
/root/cts-1.0.2.6/deqp-vk-linux-corrected.

BGR8 UINT follow-up: padded BGRA storage supports sampling and raw transfers;
BLIT_DST remains unadvertised pending native BGRA integer attachment export.
Case 91 NotSupported in 20260913T161629985260Z-cts-padded-bgr-granularity:
it requires attachment support, which padded sampling alone does not provide.
The BGR format was added to the existing CTS sampler oracle without altering
reference checks: repeat sampling Pass in 20260913T161821590699Z-cts-bgr-sampling-control,
and opaque-white border sampling Pass in the subsequent cts-bgr-border-control
experiment. The initial extra border-control name was invalid and selected zero
tests, not a process or GPU crash; evidence is retained. These added controls
are separate from the frozen 100-case target.

D24S8 follow-up: separate Z32_FLOAT depth and S8 stencil planes with GPU packed
24-bit transfer conversion. Fused normalization preserves all depth bits in
the exhaustive host arithmetic model (build/cts-next/d24-arithmetic.log).
Exact GPU mip/array/padded-copy probes Pass in
20260913T163411496978Z-d24-exact-copy-probe. Original CTS depth image copy,
three clears and three nearest/linear shadow comparison controls Pass in
20260913T163443287457Z-cts-d24-controls-000. Depth blit destination remains
unadvertised and its control reports NotSupported, not Pass. Host transfers
and multisampling of this emulated format remain unadvertised. Three target
stencil cases pass in 20260913T162648763042Z-cts-d24-stencil, bringing known
target passes to 84; this is not yet a fresh full-target run. Watchdog and
thermal guards active, no reset.

Explicit chroma reconstruction: immutable filter state selects luma-centre
reconstruction followed by full-resolution filtering. Nearest chroma ignores
chroma location; linear retains cosited/midpoint coordinates. Target 98 Pass
in 20260913T164314522092Z-cts-explicit-chroma-flrp (85 known target passes).
Two implementation-time userspace compiler crashes are retained, diagnosed and
fixed: editing an unattached NIR clone, and stale flrp_lowered state. No GPU reset.

Ten extra controls cover nearest/linear texture and reconstruction filters,
2/3 planes, 420/422 and 8/10-bit values. Initial 6 Pass / 4 Fail in
20260913T164354617458Z-cts-explicit-controls-000 exposed a CTS oracle error:
reconstructLinearXYChromaSample reverses midpoint weights. For i=2k,
i_RB = 0.5*(i-0.5) = k-0.25, so the upper sample weight is 0.75, not 0.25;
for i=2k+1 it is 0.25. This agrees with CTS's own 422 oracle and the equations at
https://docs.vulkan.org/spec/latest/chapters/textures.html#textures-explicit-reconstruction
The same inverted XY weights were found in Khronos CTS main on this date.
chroma-midpoint-reference.patch corrects just these two expressions; no
tolerances or pixel checks are removed. All ten controls Pass using the
separately built diagnostic in 20260913T164603014781Z-cts-chroma-midpoint-oracle-000.
Exact combined patch/manifest are retained. The fixed 100 target still uses its
previous diagnostic binary: its explicit-nearest case needs no oracle change.

Fresh fixed-target run: **85 Pass, 15 NotSupported, 0 Fail/Crash** in
linux-100-depth-chroma-{results,summary}.json. This corrects an earlier notes-only
misclassification of BGR granularity: the retained original result was always
NotSupported, although its experiment guard succeeded. Separate BGR sampling
controls did pass. No outcomes were changed or excluded.

Tessellation compiler groundwork: native G16G offline Metal compilation
(broadcast-lanes.metal) witnesses immediate subgroup lanes 7/15/23/31, live/final
source hints and a device-load wait. Four byte-exact encoder unit vectors pass.
The retained failing prefix-sum bundle c8ddc2d57e999f8f41dadaf80e6c5ec2aaa58cff5bd594800f3e1a8f5b64e4dd
from 20260913T165307565822Z-cts-tess-native-varyings now replays offline successfully
(1586 bytes). Native source/archive/text/commands are retained under
build/cts-next/broadcast-lanes-native; this is compiler evidence, not a passing
tessellation hardware claim. Tessellation remains an opt-in experiment.

Native integer prefix source liveness: G16G prefix-live.metal witnesses f25 for
a still-live integer scan operand. The encoder previously pinned f27 (final
read), discarding the value needed for inclusive-scan correction. A byte-exact
encoder vector passes. Guarded tessellation state evidence changes counts from
0/3/6/9 (20260913T170237920004Z-cts-tess-state-dump) to 3/6/9/12, with all four
corner triangles passing in 20260913T170513891519Z-cts-tess-prefix-source-live.
Tessellation remains opt-in while the remaining target cases are checked.

### Native tessellation integration

The nine fixed-target tessellation cases passed in
`20260913T171421738429Z-cts-tess-nine`, with native Ioto VS/TCS launches,
graphics descriptor roots, both TES variants, and internal TPS scratch.
This brings known fixed-target passes to 94; a fresh default-enabled run
will be retained separately. No target case or pixel oracle was changed.

Thirteen additional controls checked patch sizes, primitive IDs, and coordinates
for triangles/quads/isolines with equal/fractional odd/fractional even spacing.
Four patch/ID cases passed unchanged. The nine coordinate cases in the old
release use a zero-sized render area with a 1x1 framebuffer. The separately
identified `tess-controls-combined.patch` sets that area to 1x1 (current valid
usage requires positive extents); coordinate outputs and comparison remain
unchanged. All nine then passed in
`20260913T172158014701Z-cts-tess-valid-renderarea`. Original source/executable
were restored and the frozen Linux-100 executable was not replaced.
See `tess-validation-results.json` and `tess-controls-build.json` for evidence
and exact binary hashes. No synchronous tessellation dump was enabled.

Fresh default-enabled campaign `linux-100-tess` confirms **94 Pass,
6 NotSupported, 0 Fail/Crash**, with no tessellation override or synchronous
dump. Exact cases, library/guard/binary identities and four batch paths are in
`linux-100-tess-{results,summary}.json`.

BGR8 integer attachment support: target 91 and both integer blit controls
pass in `20260913T172539298620Z-cts-bgr-attachment`. The dedicated attachment
oracle then found a missing padded epilog format; after fixing it,
`20260913T172822470876Z-bgr-attachment-epilog` passed all 144 exact pixels,
including clear/reload, overlapping clear draws, mip 1 and layer 1. Physical
BGRA8 storage now agrees across image layout, tile layout, PBE and epilog.
This establishes 95 known target passes (94 in the last full run plus 91).

Scheduled queues: kernel fba91e5d9577 advertises separate scheduler entities
and dependency timelines without promising concurrent physical engines. Mesa
exposes eight Vulkan queues on that capability; existing hardware-reservation
limits remain a separate interface. The default eight-queue timeline probe
passed both consumer-first rounds in
`20260913T174324530357Z-scheduled-queues-default`. Both frozen queue cases
(62/63) passed without overrides in
`20260913T174903263267Z-cts-scheduled-queues-default` (58 C peak). This
establishes **97 known target passes**; the last full campaign remains 94.

An earlier probe incorrectly waited before submitting a binary-semaphore signal.
That invalid probe blocked in Mesa WAIT_PENDING and caused watchdog recovery;
it is excluded. The corrected probe enables timeline semaphores and checks
all copied words across four queue pairs, then reverses their roles.
