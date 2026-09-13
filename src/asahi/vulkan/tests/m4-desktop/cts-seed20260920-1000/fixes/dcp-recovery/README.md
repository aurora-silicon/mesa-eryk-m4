# Independent DCP failure during this work

At 21:06:07 UTC the existing hourly D114 callback (4164-byte input, 4100-byte
reply) caused DCP idle callback failure -95. At 21:06:11 the next flip failed
-EIO and the driver pinned its buffers. GPU jobs continued to retire. This is
the previously recorded issue in tools/README-vulkan-traces.md and
_provenance/dcp-20260913, not a shader execution failure. Its reply semantics
remain unqualified; no guessed acknowledgement was added.

Both native and lavapipe presentation stalled in the failed display state.
Increasing the per-case bound and changing the EOT single-target ABI did not
fix it. The EOT diagnostic change was reverted. The original 20-second and
longer-bound timeout outcomes are retained separately. A Wayland event pump
was also added to CTS to process configure/ping events during the blocking
render loop; it does not recover a failed display.

CTS now refuses WSI batches when m4_status reports a latched failure. Existing
experiment target-before.json files already retained that display state; the
new preflight enforces it instead of allowing misleading presentation tests.
After saving evidence and stopping GPU clients under the 60-second watchdog
and thermal guard, a guarded recovery reboot was requested at 21:43:12 UTC.
Recovery is containment for this pre-existing DCP issue, not a fix for D114.
