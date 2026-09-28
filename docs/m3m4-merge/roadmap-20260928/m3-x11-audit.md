# M3 X11 environment audit

All 3,997 previously recorded XCB/Xlib indices [3135949, 3139946) were audited
without changing original catalog records. Final counts: 243 Pass,
3,753 NotSupported, one Fail. Changes: 217 original NotSupported now Pass,
six original Fail now Pass, 20 original Pass still Pass; one former
NotSupported now exposes an intermittent Fail. Indexed evidence is retained
in m3-x11-audit-summary.json. CTS and GPU binaries are unchanged.

The remaining failure is index 3136712,
dEQP-VK.wsi.xcb.maintenance1.present_modes.heterogenous.mailbox_fifo_fifo_relaxed:
acquireNextImage timeout, no observed GPU fault. Single-case, original 16-case
sequence, and 20 consecutive diagnostic retries pass. This does NOT explain
or fix the original intermittent failure. Keep it open. Managed-window
XMoveWindow experiments were rejected: xwininfo proved Weston did not move
the windows, regardless of the helper's request log. Original layout helper
restored; do not claim tiling fixed this XCB case.

Diagnostic infrastructure failures retained separately: missing xwininfo caused
one guarded helper abort, and terminating a stale SSH process raced a resumed
connection, yielding BrokenPipe at guard output and another watchdog recovery.
Neither is evidence of a GPU crash. Final 20-retry guard completed normally.
Evidence directories: build/driver-roadmap-20260928/m3-x11-environment-audit,
m3-x11-audit-remaining, m3-xcb-present-repeat, m3-xcb-present-repeat-r2,
m3-xcb-present-tiled-repeat. Main full catalog remains paused at 3,139,946.

cts-regression.py now honors OUTPUT/STOP between batches and stops on the
first test failure even when GPU/guard health remains good. Separate test
failure from guard health in summaries; retain all child receipts after a
controller interruption. The first audit has 1,120 child outcomes even though
its interrupted outer summary only reached 1,104.
