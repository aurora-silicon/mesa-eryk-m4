# M3 compute timestamp boundary reuse

G15S already exports firmware compute start/end timestamp destinations. Extend
the existing opt-in G16G boundary reuse to G15S, keeping the historical
`HK_M4_COMPUTE_START_TIMESTAMP` and `HK_M4_COMPUTE_TAIL_TIMESTAMP` option names.
Both remain false by default. Timestamp fanout still requires its separate
kernel feature and is not enabled on M3 by this change.

The start path preserves resets, dependencies, occupied timestamp destinations
and query-copy visibility boundaries. The tail path attaches only to the actual
nonempty tail and excludes reset streams. Neither path invents a host timestamp
or removes application operations. The only discarded command is a standalone
timestamp no-op whose destination moves to the real successor's start.

On M3 kernel 7.1.9-dirtyroom-asahi1+, module SHA256
`4cb9731c612cef37201ee3839c8c3868f7c76a52decaaed2fb536e3930191fff`,
the existing vulkan-query-boundary-probe passes modes 7, 8, 9, 10, 11 and 12
with both flags enabled. The logs retain ordered nonzero timestamps, exact
CPU/GPU query copies and unchanged fill data. Mode 8's architecture log proves
the real compute command uses ts_start=1 (the optimization was exercised).
The later modes cover GPU resets in completed submissions, separate command
buffers in one submit, and ordered separate submissions. Each sequence used
the 60-second watchdog and the normal all-channel thermal guard.

This is focused boundary qualification, not full CTS or a performance claim.
The new M3 library is isolated under /opt/asahi-m3m4-compute-timestamps; the
desktop and benchmark defaults are unchanged. Full Satisfactory replay with
this library and broader query CTS remain to be qualified.
