# Immediate CTS failure fixes — 2026-09-17

The user stopped the full 3,206,030-case retest and requested immediate fixes.
The stopped campaign has 21,481 terminal observations: 11,032 Pass, 10,092
NotSupported, 348 Fail and 9 Timeout. Two interrupted segments lack final guard
completion; their QPA results are observations, not clean-run qualification.
No full Vulkan 1.4 conformance result is claimed. Do not resume automatically.

Original failure list: `VULKAN_14_RETEST_FAILURES.tsv`.
Per-case follow-up: `VULKAN_14_FIX_RESULTS.tsv`.
The raw baseline remains unchanged; corrected-test and fixed-driver results
are separate attempts and are not merged into that baseline.

## The 348 blit failures were a CTS baseline mismatch

Every failing image case uses scaled formats. The original CTS executable
contains previously diagnosed scaled-reference bugs. Earlier campaign passes
used separately corrected CTS builds. Selecting the uncorrected executable
for a regression comparison reintroduced those known test failures.

All 348 cases Pass on the unchanged current GPU driver with the already-built
`deqp-vk-seed30-scaled-blit`: 9.861 seconds, guard pass, peak 62.75 C. No new CTS
source modification was made. These are corrected-CTS passes, not passes of
the unmodified release and not 348 driver regressions.

Evidence: `20260917T173441961965Z-cts-immediate-scaled-blits-corrected`.

## Sparse-buffer metadata performance fix

The nine timeout cases were previously NotSupported before sparse features
were enabled. All query memory requirements for large, initially unbound
sparse buffers, iterating powers of two up to the advertised buffer limit.
They timed out at 60 seconds in the broad 20-worker run. An isolated diagnostic
also timed out at 20 seconds. Its native stack is in hk_update_sparse_residency;
process accounting shows about 15 seconds in userspace and 0.29 seconds in the
kernel. This isolates metadata bookkeeping rather than GPU execution.

hk_buffer.c now uses Mesa's existing BITSET_SET_RANGE/BITSET_CLEAR_RANGE helpers
instead of updating each 16-KiB page bit separately. Buffer-alias cleanup skips
whole absent 64-MiB leaves and clears existing leaf spans in bulk, preserving
neighbouring entries. Canonical alias addresses are filled per populated span.
No buffer limits, sparse capabilities, page mappings or test deadlines changed.

All nine original timeout cases now Pass with the **unchanged original CTS**
executable and 60-second limit: 16.572 seconds for the whole parallel run,
slowest case 16.46 seconds, peak 67.75 C. All 32 byte-checked residency oracles
also Pass, including partial pages, rebinding and addresses above 4 GiB.
The 14,319-case non-device-group sparse-resource regression also completed: 8,860 Pass,
5,459 NotSupported, zero failures/timeouts, 499.166 seconds. This is targeted
sparse-resource qualification, not the stopped full Vulkan 1.4 campaign.

Installed Vulkan SHA256:
`5cd202d31d68cae60f9cacc2768bad5142cf0e87c3eea8473c4fa78060246942`.
Previous Chromium-fixed build, retained as an atomic-install backup:
`c932739077989a92cd483a361f38f7a55cd257dc8f6b5061754f766810f6eb6d`.
Kernel unchanged. Fresh applications use the fixed library; the greeter keeps
its existing compatible mapping. No reboot needed for the userspace update.

Evidence under `_provenance/experiments/`:

- `20260917T173800382874Z-cts-sparse-profile-proc`: isolated timeout/CPU stack.
- `20260917T173953395831Z-cts-sparse-metadata-ranges`: 9/9 original CTS Pass.
- `20260917T174054057887Z-cts-sparse-metadata-residency-oracles`: 32 oracles Pass.

Selected evidence is also versioned beside the tools in
this directory.
Every hardware experiment used the 60-second watchdog, fresh thermal readings
at 0.5-second intervals and 95 C cutoff. The host profiling wrapper is bounded
and reads process/kernel stacks; it does not renew or bypass the guard.

Full sparse regression evidence: `20260917T174132534460Z-cts-sparse-metadata-regression`;
guard pass, peak 69.75 C.
