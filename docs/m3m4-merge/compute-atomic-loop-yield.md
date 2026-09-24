# Native compute atomic-loop forward progress (M4, 2026-09-24)

A Satisfactory DX11/DXVK compute reduction could hang both the application and
an unmodified GFXReconstruct replay. Its workgroups reduce luminance locally,
then retry a global compare/exchange critical section to update the maximum.
A standalone reduced shader reproduced the timeout, without game execution.

The compiler now emits native YIELD with a register-allocated 16-bit delay
operand (8) at the backedge of AGX3B compute loops containing global atomics.
This preserves the unbounded retry, memory operations and result. It does not
replace the shader, bound retries, or change game settings. The yield is a
scheduling barrier and retires pending scoreboard operations before yielding.
AGX2, AGX3C, other shader stages and loops without global atomics are unchanged.

## Evidence

Guarded M4 experiments in dirtyroom_m4/_provenance/experiments:

- 20260924T193015087780Z-satisfactory-reduction-repeat-uncached:
  original binary passed once, then timed out with identical uncached input.
- 20260924T195318266542Z-satisfactory-reduction-no-lock-path:
  the identical binary passed 20/20 when the maximum was prepopulated, avoiding
  the contended critical section.
- 20260924T195507937734Z-satisfactory-reduction-loop-yield-100:
  diagnostic native yield passed 100/100 exact buffer comparisons without
  changing register allocation, lock protocol or loop termination.
- 20260924T195559458712Z-satisfactory-reduction-loop-padding-control:
  replacing the six-byte yield by three two-byte scheduler NOPs timed out.
  The successful yield is not explained by the changed branch displacement.
- 20260924T195804769867Z-satisfactory-reduction-native-yield:
  the compiler-managed implementation passed 100/100 exact comparisons;
  all-channel temperature peak 65 C, watchdog 60 seconds throughout.

The 64-iteration diagnostic passed but is not a proposed fix. Its instrumented
run completed all 4096 workgroups in at most 14 iterations; none hit the bound.
Single-subgroup workgroups, SSBO instead of texel-buffer storage, disabled
scheduling/cache hints and materialized branch conditions did not solve the
original hang. This supports contention/forward progress as the failure mode;
it does not establish undocumented hardware scheduling details.

Host tests cover native packing, atomic encodings, scoreboard allocation,
control flow and the compiler backend. A new backend test checks that YIELD
waits for both its own asynchronous source and unrelated outstanding loads.
The standalone SPIR-V variants, original proprietary startup trace, full fault
dumps and experiment records remain in the local-only gpu-replay-bench repo.

Original application/replay qualification and M3 qualification are separate
from this reduced M4 validation; no M3 run is claimed here.
