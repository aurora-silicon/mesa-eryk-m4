# Rendering optimization checkpoint, 2026-09-29

These are matched before/after results for the experimental memory-preamble
profile on the final publication candidate, compared with the same binary's
ordinary conservative configuration. Values are median FPS from three warm
30-frame timings per variant, alternating A/B order, with reference checks
before and after. They are replay results, not whole-game FPS or improvements
over every previously tested experimental configuration.

| Capture | M3 baseline | M3 preambles | M3 gain | M4 baseline | M4 preambles | M4 gain |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Satisfactory DX11 | 2.0415 | 3.4967 | 71.3% | 0.9542 | 2.0033 | 109.9% |
| Frostpunk DX11 | 1.6604 | 2.4298 | 46.3% | 0.8281 | 1.3441 | 62.3% |
| Satisfactory native Vulkan | 2.3094 | 3.2728 | 41.7% | 1.2622 | 2.0363 | 61.3% |

Exact-image checks gate Satisfactory; Frostpunk uses its existing reviewed
sparse RGB policy accepted by the user. All watchdog and thermal guards were
retained. M4 thermals differ between variants (DX11 baseline peak102.5C versus
preamble90.75C), so this measures the actual guarded workload, not an isolated
fixed-clock compiler effect. Native Vulkan retains the documented adapted
capture and internal-resolution limitations.

DX12 checks are now bit-exact with/without preambles on both GPUs. M4 requires
the smaller packet8 diagnostic memory setup; no new comparable DX12 performance
number is claimed. The earlier roughly5FPS native result used additional
experimental direct-buffer optimizations. Current direct-buffer controls regain
much of that speed but retain image differences, so they are excluded from this
qualified table. The latest control split adds diagnostic knowledge, not a new
qualified speedup beyond these paired results.

Preambles and direct-buffer shortcuts remain opt-in. The user's normal desktop
Mesa was not replaced. Machine-readable numbers and evidence paths are in
`preamble-publication-validation.json`; compiler/profile and memory controls are
in `native-profile-controls.md`. Private images and captures remain in the
local-only gpu-replay-bench repository.
