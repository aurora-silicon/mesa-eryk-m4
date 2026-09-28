# M3 frozen Vulkan catalog complete

All 3,206,030 zero-based Linux catalog entries have one retained outcome.
The ordered index/name pairs were checked against the pinned catalog hash.
Original totals: 1,574,713 Pass; 1,631,280 NotSupported; 31 Fail; one Crash;
one Timeout; four QualityWarnings. Original records are not overwritten by
successful follow-up tests. Catalog and database hashes are adjacent.

The 31 failures comprise one sample-location limit mismatch,24 advanced-blend
ordering cases and six XCB cases from the incorrect headless GL stack. Their
fixes and follow-up results are recorded in cts-fixes-20260927 and this roadmap's
X11 evidence. The crash was the fixed premature TA progress timeout; the Xlib
timeout was the fixed occluded-window test setup. Four callback allocation
warnings remain separately explained in allocation-quality-warnings.md.

The separate 3,997-case X11 audit exposed an intermittent XCB acquire timeout
that did not reproduce in80 follow-ups; its cause remains open. Completing the
catalog does not close that issue, constitute a single-revision campaign, or
qualify official Vulkan conformance. The recorded existing CTS binary contains
prior corrections. Final normal display manager remained active.
