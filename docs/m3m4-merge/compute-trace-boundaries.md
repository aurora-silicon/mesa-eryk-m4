# Correlating native compute timings

`HK_G17P_COMPUTE_TRACE=true` now emits a BEGIN marker when a command stream is
allocated, includes its base in each compute-record line, and emits SUBMIT
with its final extent and linked-stream flag. This separates recordings that
reuse the same GPU virtual address. A union of all dispatches ever recorded
at an address can otherwise attribute later timings to stale programs.

Correlate only complete recording/submission/retirement sequences with equal
counts and matching extents. Reject unknown starts and linked streams unless
the analysis explicitly understands their continuation buffers. Timing belongs
to a whole submitted stream; do not divide it among its programs without
individual dispatch timestamps. Instruction counts and scratch sizes are not
measurements of time.

A guarded M3 Satisfactory replay with tracing completed all 30 measured frames
and preserved exact RGB at frames 1, 16 and 31. The retained analysis matched
10,377 command streams; unmatched address groups were excluded. This is
profiling evidence, not a benchmark score. The environment option defaults to
off and adds no logging in normal execution.
