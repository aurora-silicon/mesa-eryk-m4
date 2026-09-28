# Separate the native replay performance profile from correctness

Final publication candidate b656c92f was compared on the same current M3 boot,
capture, packet policy, and guarded runner. The earlier native-read-early binary
still gives 4.66 FPS in an unscored screenshot check; current ordinary preambles
give about 3.09. Restoring all six experimental options gives 4.79, but fails the
current corrected reference (346/377/435 changed pixels, maximum 3/3/2).
These screenshot measurements are diagnostic, not qualified timing results.

Starting with the same two preamble flags, descriptor prefetch plus compact
prefetch alone matches every checked RGB pixel. Scaled-format branching alone
also matches. Direct buffers alone exactly reproduces the full-profile pixel
differences and gives 4.64 check FPS. Thus the direct-buffer control is the main
performance/correctness tradeoff in this test; the new publication sequence did
not cause the slower ordinary result. It remains disabled by default. This does
not qualify scaled-format branching globally: the prior Lethal Company failure
still needs rechecking. No reference or tolerance was changed.

M4 DX12 ordinary packet32 stops at the existing 1 GiB available-memory floor,
first with its desktop and again with a private software headless compositor.
Both failures retain guards, logs and memory telemetry; neither is a GPU fault
or a successful replay. The desktop is restored and the activity ABI is healthy.
The prior successful DX12 run used packet8, so comparison requires retaining
that difference rather than blaming the compiler or relaxing the memory guard.
