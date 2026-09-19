# Complete physical ownership, qualified 2026-09-19

Kernel79f480d290c9 completes the parameter-buffer ownership protocol:
manager+0xc, RunTA+18, TA register1c830 and fragment register1c838 all select
the same reserved owner. Descriptor-only1c617 FAILED boot; both the failure
and successful recovery are recorded in the kernel tests note. Native M4
captures ground both engine selectors. Do not deploy overlap on older kernels.

With complete ownership, replay142127 preserves every pixel and all ten
fragment query totals; all18 command timestamps are present in each frame.
Whole GPU spans6.163..6.242ms, versus6.354..6.467ms with overlap disabled on
the same kernel (142349). Five-frame traces are unchanged2512x1552.

Live menu142153 completes60seconds without clicks or faults:117 samples,
mean58.26FPS, median59.92, final12mean59.99. All samples retained including
minimum21.94. Peak86C; watchdog and six thermal channels active. This passes
the situation that failed before, but is not broad stability or the5ms goal.
Enable early tiling by default again with this corrected kernel. Installed
librarya02c681565b0632fc0185deb08fcfcccecd8d41c6b39757a866472977ccb7e08.
Installed-default replay142944 passes exact pixels and counts at6.166..6.260ms.
Installed-default menu143011 passes120seconds (peak88.75C); complete HUD
samples are retained in physical-owners-installed-menu-factorio-result.json.

The first longer attempt142816 FAILED during startup with SURFACE_LOST_KHR,
GLXBadCurrentWindow and guest processSIGSEGV, without a kernel GPU fault.
Retain this unresolved window/process failure; do not claim it is explained
or erased by the subsequent successful run. The142944 hypothesis prematurely
called it unrelated; only the absence of an accompanying GPU fault is known.

Earlier failed qualification and diagnosis are retained below as history.

# Live validation overrides the replay qualification

134928 normal-menu run TIMED OUT at GPU job12904 after about20s of animation.
Guard retained sealed dump SHA8c0f99e181ab277beaebe4920ce4f9cea8aa37dd14eee17016fdf97333057f9c
and recovered the desktop. Peak90.5C; not a thermal cutoff. Early tiling is
DISABLED by default again. b8698ecbe7d's default-enablement qualification is
withdrawn; below replay results remain real but are insufficient for live use.

Initial fault diagnosis: the active render's requested PB slot is1 but its
parameter manager's current hardware slot at+0xc is0. Only slot0 in the retained
hardware parameter table is populated, with another owner's page-list address.
The encoder writes id at+0x10 while leaving+0xc zero. Firmware full rebuild
2eb70/2ec90 indexes the table from+0xc, NOT the command's logical PB id. This
is a strong aliasing lead for overlapped stages; fix/qualification pending.
Keep stage overlap opt-in until live validation succeeds.

# Fragment dependency overlap and shader-owned queries

Enable capability-gated G16 early tiling by default, using corrected kernel
c40864f9d5f8. Explicit TA-to-TA and FS-to-FS predecessor chains preserve the
dependency graph; each FS also waits for its own TA. Never infer completion
ordering from firmware queue order. Compute, command-buffer boundaries, start
timestamps and dependencies targeting vertex stages remain conservative.
Requires the corrected kernel; the earlier a820b7032400 prototype timed out.

Remove obsolete statistics firmware-timestamp ownership. EndQuery publishes
availability through ordered GPU writes, so statistics use normal 32-bit
availability, without firmware binding or timestamp-reset boundaries. Real
timestamp queries retain their firmware ownership and visibility boundaries.
HK_M4_QUERY_COMPLETION is retired; it no longer changes query layout.

All runs use watchdog60s, six thermal sensors0.5s, cutoff95C, full CPU ceiling,
unchanged native Factorio five-frame capture at2512x1552. No shader or quality
changes. Peak temperature across these six validation runs is72C.

- 133357: corrected early tiling passes, pixel-exact; whole GPU submission
  6.179..6.334ms versus prior deferred-statistics6.409..6.450ms. The original
  frame-cost tool counted renders alone; its updated copy includes all eight
  compute commands and checks complete command coverage before reporting a
  whole submission. These are hardware spans, not live FPS.
- 133455: ordinary statistics layout passes, every pixel and all ten query
  results exact (five40000 and49448124/49451888/49457522/49463366/49470745).
- 133822 and134203: exact GL statistics oracle passes, including one-pixel
  and odd edges, CPU/GPU query copies and queries spanning submissions.
- 133958: eight targeted synchronization2 cases PASS, covering fragment and
  vertex consumers after fragment/compute writes in two formats. The outer
  report says missing_success_evidence because the requested summary string
  was not printed by cts-sample; exit0, retained QPA/results and all eight
  per-case Pass records establish the outcomes. No hardware failure.
- 134102: final built defaults pass original replay, pixel-exact and exact
  query counts; complete timestamps retained in frame-cost.json.

This is targeted qualification, not full Vulkan CTS or the5ms goal. Full CTS
remains deferred. The pre-existing Buoy work is outside this change.
