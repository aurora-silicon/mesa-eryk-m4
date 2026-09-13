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
Case 91 Pass in 20260913T161629985260Z-cts-padded-bgr-granularity (82 known passes).
The BGR format was added to the existing CTS sampler oracle without altering
reference checks: repeat sampling Pass in 20260913T161821590699Z-cts-bgr-sampling-control,
and opaque-white border sampling Pass in the subsequent cts-bgr-border-control
experiment. The initial extra border-control name was invalid and selected zero
tests, not a process or GPU crash; evidence is retained. These added controls
are separate from the frozen 100-case target.
