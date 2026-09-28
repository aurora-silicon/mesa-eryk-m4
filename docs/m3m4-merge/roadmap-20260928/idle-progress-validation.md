# Idle-aware guarded replay qualification, 2026-09-28

The separate activity-v1 kernel endpoint is hardware-qualified on both GPUs:
eight exact mixed-facing fragment probes each observe healthy pending and idle
epochs. M3 kernel d6e3ec400ff3 (publication 90e0d586066a), M4 1904f8616760.
No Mesa flags, desktop packages, thermal limits or watchdog timeout changed.

The first CPU monitor only followed process groups and missed the replay's
new-session child. That attempt failed and recovered by watchdog; retained as
20260928T160733362592Z. The corrected monitor follows the launcher process tree,
validates its birth identity, and requires CPU time advancement between two
matching healthy idle snapshots. Busy GPU work cannot use CPU activity as
progress. All eight unit tests pass, including new-session descendants and PID
reuse. The total experiment bound remains mandatory.

The M3 CPU-cap loop also overwrote the guard's deadline variable. Renaming its
CPU limit restores the advertised total bound. The real selection/CPU-cap-loop
regression test rejects all eight original combinations and passes the fixed
version. The independent 60-second watchdog was not removed or extended.

With all shader disk caching disabled, the previously failing compressed M3
control now completes in 99.623 s, with nine idle-CPU renewals and one retirement
renewal, maximum 85.25 C. All three images are byte-exact with the earlier
completed compressed control; its known captured-memory-alias discrepancy is
not resolved or approved by this guard fix.

Ordinary uncompressed cold replay also completes on both devices and all three
4K images match the corrected ordinary reference exactly:

| Device | Setup plus replay | Peak monitored temperature |
| --- | --- | --- |
| M3 Pro | 100.436 s | 87.0 C |
| M4 | 149.754 s | 109.75 C |

These are cold validation durations, not performance scores. Both normal desktop
sessions are active after validation. Earlier faults remain retained, and this
does not establish the cause of every historical reset.
