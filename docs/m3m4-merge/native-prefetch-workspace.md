# Native uniform-loader workspace

The opt-in UBO plus compact descriptor prefetch path exposed a software
capacity mismatch in a native Vulkan workload: command-buffer recording
rejected a graphics program with 84 uniform ranges. The loader admits up to
128 chunks, but its conservative GPU0 bound is 78 bytes per chunk, exceeding
the former 6144-byte graphics workspace before reaching that chunk limit.

Size the graphics workspace for 128 * 78 bytes plus the 90-byte graphics
envelope. Keep the existing chunk limit, range validation, uniform-register
bounds and final instruction encoder checks. This changes host allocation
capacity; it does not relax a hardware encoding limit or enable prefetch by
default.

Validation: the added 128 disjoint scalar-range test failed at the bound check
before the fix. Afterward the Ioto core host suite passes 7017 checks, including
program generation on G15S and G16G and rejection of a 129th chunk without
modifying the output bound. AArch64 Vulkan library builds successfully.

The guarded M4 replay completed all 30 measured frames after this repair
(20260925T165516430783Z-satisfactory-native-prefetch-bound; peak 108 C).
This validates removal of the recording refusal, not the experimental prefetch
feature: its images differ from the default path and the 24.51 s diagnostic
interval does not improve on the 24.03 s control. Keep those options disabled.
Private game traces and images remain in the local benchmark repository.
