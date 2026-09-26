# Native register budgeting

AGX3B uses dynamically allocated register/threadgroup/stack storage. Do not use
G13's workgroup-size register table or its static wave-saving rematerialization
heuristic for this encoding. The qualified192-half instruction encoding limit
and normal spilling when demand exceeds that limit remain in effect. Other
encodings retain their existing policy.

Architecture basis: Apple's “Explore GPU advancements in M3 and A17 Pro” and
“Discover new Metal profiling tools for M3 and A17 Pro”:
https://developer.apple.com/videos/play/tech-talks/111375/
https://developer.apple.com/videos/play/tech-talks/111374/
These describe dynamic allocation and occupancy; the numeric encoding limit
comes from our native ISA evidence, not those presentations.

306 compiler units and five retained hot compiler inputs pass. The1024-thread
input14882a61 changes from13008 instructions,104 half-registers,136 scratch
bytes and228/224 spills/fills to11772 instructions,191 half-registers and no
scratch or spills. This is an offline compiler comparison, not a timing.

Eight CPU-exact integer/barrier cases cover64,256,512,1024 threads with32/80
live values. Every byte matches on lavapipe,M3 and M4. The generator is
src/asahi/compiler/test/probes/register-budget-oracle.py; run its generated
run.py with vk-buffer-oracle inside each target's watchdog/thermal guard.
24 pinned compute/control-flow/memory CTS cases also pass on both machines.
M4 evidence:20260926T131143414388Z-dx12-dynamic-registers-oracle and the matching
dx12-dynamic-registers-cts experiment. This is targeted qualification, not a
full conformance claim.

The unchanged4K-output Satisfactory DX12 capture passes its existing image
budget at check131123374432. Timings131151578886,131212919755,131234801888 give
3.620116,3.635730,3.651911FPS (median3.635730), versus3.629437 previously.
Treat full-scene performance as neutral within run variation. No quality,
clock,capture or image-tolerance change. Normal desktop Mesa is unchanged.

Qualified binary SHA256:
417259afea284d8eda1328e20a4265f6dcabea08d301e15ca680af9c1d612b40.
Source/binary/oracle outputs/guard logs and replay images are retained in the
local-only gpu-replay-bench provenance/satisfactory-dx12-optimization-20260926.
