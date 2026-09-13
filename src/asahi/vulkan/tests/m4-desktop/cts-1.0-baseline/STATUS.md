# Vulkan 1.0 random baseline — 2026-09-13

100 cases selected before execution from233795 must-pass entries with Python
Random(20260913). No exclusions/redraws. Original outcomes:35 Pass,19 Fail,
41 NotSupported,5 userspace crashes,0 timeouts. Results and exact names are in
results.json/cases.txt; original logs remain at the listed workspace evidence
paths and on the target. Unsupported results are not passes. This is a sample,
not conformance. Driver:Apple M4(G16G B0), native Asahi Vulkan, library
19afd1a56957886e18ecbaf88f63b473700c69a5cbb3011df0227dbc783369bb.
The four additional info tests passed, independently of the100 sample cases.

All batches completed with a60-second watchdog lease and six-channel thermal
monitoring. Highest sample temperature58.75 C. No GPU hang/watchdog reset.
The existing DCP idle fault occurred at uptime3610 before CTS(3999); the render
GPU continued to retire jobs. A guarded reboot subsequently recovered display
state; default SDDM remained enabled/active.

Observed failures include compiler refusals(imad, texture_sample, texture_load,
image_write, ld_tile), multi-plane image handling,16-bit shader inputs and one
texel-view pixel mismatch. These are observations, not a blanket kernel blame.
Case45's render-pass abort also crashes under lavapipe(control
20260913T104404951360Z-cts-1-0-old-renderpass-control). Pinned CTS returns the
input-aspect vector by value then passes .data() from that temporary after its
lifetime ends(vktRenderPassTests.cpp:680,1190). Keep this historical test bug
separate from driver defects; the original baseline test code is unchanged.

First fix:seed20260913 drawn again from the19 Fail cases selects index10,
integer cube sampling with explicit tiny LOD. Fresh hardware run
20260913T104946283939Z-cts-cube-fix passes against CTS's reference image.
See compiler/test/probes/README-g16g-cube-register-lod.md for native evidence.
Unit checks:6262 texture-core checks,33 focused optimizer/staging tests and
cube-lowering-only pass. The broader historical graphics suite has unrelated
stale expectations. ExplicitLodStagingAndLifetime fails both with this change
and at baseline HEAD3b3a3df7ce7; its baseline log is retained in
build/cts-fixes/baseline-explicit-lod-test.log. Do not claim the full unit suite
passes.
