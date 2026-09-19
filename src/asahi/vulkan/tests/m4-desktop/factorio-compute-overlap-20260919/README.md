# Compute-crossing fragment dependencies, September 19

Kernel ca1ef25e4885 and Mesa f4c70434d1d, private ICD b6dba9fd.
The new option remains disabled by default. The native five-frame capture
retains all 10 render and 8 compute commands per frame, at 2512x1552.

Unbounded early tiling through compute preserves exact pixels and all ten
fragment query totals, but regresses whole-submission GPU spans to
6.934..7.104 ms. The same binaries with the option disabled take
6.152..6.290 ms. Fragment execution remains about 4.3..4.4 ms.
The first fragment starts about 0.9 ms after its TA, while later TA stages
run ahead. This is evidence of a scheduling problem, not shader ALU cost.
The cause of that scheduling delay is still under investigation.

Both experiments passed watchdog/thermal guards, no GPU faults. These are
short replay results, not live application or full CTS qualification.
The query-checking replay helper now asserts exact counts automatically.
