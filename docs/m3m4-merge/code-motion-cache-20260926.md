# Isolate code-motion compiler variants in the shader cache

AGX_CODE_MOTION selects different fragment and compute compiler behavior, but
was omitted from Honeykrisp's compiler flags used by both pipeline UUIDs and
disk-cache keys. A no-motion diagnostic could silently reuse ordinary GCM
fragment binaries. Reserve the top three bits for unset, none, GCM, GCM without
value numbering, and LICM, keeping all existing backend debug bits disjoint.
Unknown values have the compiler's existing no-motion behavior.

The first M3 DX12 experiment 20260926T090936278312Z is inconclusive for this
option because it used the old cache key. With this fix, cold compilation in
20260926T091149272152Z produces the new shader hashes and passes the unchanged
three-image DX12 budget (max4/255, <=32 differing pixels in each 4K image).
Compiler overrides and binary hashes remain recorded in benchmark evidence.
The default compiler policy and desktop installation are unchanged. Performance
qualification of no-motion is separate; this commit only fixes cache identity.

Build validated for ARM64. Hardware check retains the 60-second watchdog,
retirement-gated renewal, 0.5-second thermal samples and existing temperature
limits. Full private evidence is retained in gpu-replay-bench.
