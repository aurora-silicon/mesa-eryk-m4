# Final paired allocation validation

Mesa 8d7f8eeeaf4, rebuilt SHA
2e2f0cb06def7e6a7407059cb4da2bd295a486965e57dd60280908f61ca2b688,
is installed in a private allocation-20260928 prefix on both machines. Both pass
178 external-memory/render cases with six unsupported. Each passes ordinary
command-pool CPU mapping failure and eager shader failure with disk cache off:
OUT_OF_HOST_MEMORY, followed by successful same-device checked fill/readback and
zero callback-owned allocations at destruction.

M3's warm-cache case exposed a test assumption: shader deserialize can fail and
fresh compilation can legitimately recover. It returned success after a checked
GPU fill. The initial test rejected this and aborted; no GPU fault occurred.
The corrected warm-cache probe explicitly permits this already-checked fallback
and still requires a second successful readback. Cold and ordinary command-pool
checks remain strict OOM expectations. M4 returned OOM in all three probes.
Original failure receipts are retained; this is not a new driver regression.

M3 CTS paused cleanly at 3,139,686 outcomes and resumed only missing indices with
an appended identity review after this validation. Original outcomes are intact.
The earlier M3 fault-snapshot reservation hardware qualification is recorded in
multiple-error-fences.md and supersedes its original source-only checkpoint.
