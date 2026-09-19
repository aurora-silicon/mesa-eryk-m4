# Exact fill coverage and timestamp visibility diagnostic

HK_M4_BATCH_META_FILLS=true (default false) finalizes an internal address-fill
stream's write ledger only when its recorded dispatch count covers every
command. Unknown dispatches, destination overflow and memory/timestamp
boundaries remain conservative. Adjacent query resets can then share a single
ordered compute stream; the boundary before firmware timestamp writes remains.

Native ARM64 build passes. 20260919T045657810240Z verifies that 64 resets
actually merge (commands=64, merged=63), but a subsequent WAIT query copy
hangs. This is NOT hardware qualification. The guard saved firmware state,
rebooted and restored autologin. Keep this option disabled by default.

Safe mode 6 of the query-copy probe reproduces unavailable queries on CPU
and GPU (20260919T050003379757Z). The same work with the GPU reset in its own
completed submission passes exact CPU/GPU results and data
(20260919T050203810093Z). This points to the internal visibility boundary;
a kernel dependency change is under investigation, not yet qualified.

The boundary probe is mirrored here. Mode 9 completes GPU reset before
recording timestamps; mode 10 additionally completes timestamps before copies.
Both use NO-WAIT copies and valid command-pool reset semantics. Run only
under workspace tools/gpu-experiment.py with the watchdog and thermal guard.
Do not run WAIT modes 3 or 5 until safe mode 6 passes.

Discard preliminary controls 050116 (accidental host reset) and 050140
(individual command reset without pool flag). Their successors correct those
probe errors; they are not supporting qualification evidence.
