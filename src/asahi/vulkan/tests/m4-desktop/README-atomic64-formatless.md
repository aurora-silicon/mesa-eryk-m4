# M4 formatless integer64 image access (2026-09-16)

G16G supports R64_UINT/SINT storage image and storage texel-buffer reads and
writes without a shader format qualifier. The physical RG32 storage conversion
already packs and unpacks logical 64-bit texels; this change advertises the two
format capabilities. Signed and unsigned 64-bit payloads are preserved.

Eight independent probes cover signed/unsigned image and buffer load/store,
including returned pre-store values and post-store readback. All eight pass
lavapipe and guarded M4 execution. Generator: atomic64-image-formatless-cases.py.
The buffer oracle requests the two core formatless device features when
M4_ORACLE_IMAGE_FORMATLESS=1 is set.

The full 4,704-case frozen group passes with the experimental gate: 1,968 Pass,
2,736 NotSupported for ray pipelines (1,312), sampled R64 (768), or exclusive
compute queues (656). Evidence: 20260916T154327737064Z-dx-image64-formatless-r3,
178.796 runner seconds at 12 workers, peak 70 C. Default combined qualification: 6,060 cases, 3,028 Pass / 3,032 unsupported
 dependencies, zero failures, peak 62.75 C:
 20260916T154722438094Z-dx-image64-formatless-default-regression.
 All 108 independent buffer/shared/image/formatless/alias/scratch configurations
 pass on the default driver, peak 59.75 C:
 20260916T155207393115Z-dx-image64-formatless-default-oracles.

This exposed a kernel resource-lifetime problem before qualification passed.
Released GEM shmem files queued page eviction to one delayed_fput worker; a
stream of short-lived CTS clients outpaced cleanup and exhausted memory. The
first full run failed; the second was stopped deliberately as memory ran out.
Both records remain under atomic64-formatless/. Three implicated cases passed
after reboot. There was no matching native GPU retirement timeout in the
retained allocation-failure logs; later VM timeouts are not proof of one.

The Rust shmem GEM release fix retains its private backing file through GEM
cleanup, then releases that reference synchronously, after DMA mappings and
page pins are removed. Imported objects have no private file. The exact same
1,000-case prefix passes before/after (392 Pass / 608 NS); sampled Shmem peak
falls from 8,249 to 1,701 MiB. Full group Shmem peaks at 1,716 MiB with at least
10,819 MiB available. Kernel evidence lives under tests/shmem-reclaim/.

Host suite: 104 Pass, the same 11 known failures. Mandatory hardware bounds:
60-second watchdog, six thermal channels every 0.5 seconds, 95 C cutoff,
12 CTS workers, 60-second per-case limit. CPU thermal policy remains active.
Frozen catalog indices and SHA256 are retained; skipped dependencies are not
claimed as successful tests. Sparse image64 and sampled R64 remain pending.

Qualified experimental Mesa SHA256:
a9f1dbedf5de0d495cc67f9ada0376562c08219b7409fa3f9b8e51b36b58ff0f.
Default candidate SHA256:
37d5e4283c8c15559cc8f76aab6ab5dacac9a0a74ed3f83a97096b7dbd51d382.
Kernel module SHA256:
a5affc7dcc90cd7b29ae230ff7324fbd8712d9aadab7419882038e16b95a4f86.

Kernel fix: 945d4f48a077; compressed evidence follow-up: 353e76a9660f.
The regenerated missing-tests index has 743 groups / 1,655,966 non-Pass cases,
1,968 fewer than before. `cts_deqp_backend.py` now records memory every five
seconds; optional M4_CTS_MEMORY_DIAGNOSTIC=1 adds process/GPU-client/worker
snapshots. It neither renews the watchdog nor changes progress/thermal gates.
The missing-tests mirror is synchronized with the accumulated qualified overlays.
