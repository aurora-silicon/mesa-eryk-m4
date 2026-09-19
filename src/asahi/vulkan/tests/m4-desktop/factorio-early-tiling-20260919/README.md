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
