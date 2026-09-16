# M4 buffer integer atomic64 (2026-09-16)

G16G implements buffer 64-bit integer add, signed/unsigned min/max, AND/OR/XOR,
exchange and compare-and-swap, including returned values. The Vulkan buffer
feature and KHR_shader_atomic_int64 extension are enabled. Shared and image
atomic64 remain separate work; this is not a Steam-readiness claim.

The native hardware subset qualified earlier only supports non-returning
unsigned 64 min/max. Vulkan operations now use a device-wide lock around native
32-bit atomic halves. A winner releases before leaving the retry loop, avoiding
a SIMT lane waiting for an inactive winner. Fragment helper invocations skip the
lock. Device fences surround the update. This deliberately serializes 64-bit
operations and can be expensive; correctness qualification does not establish
game performance.

Lowering runs after Poly splits geometry variants. Expanding earlier left the
count variant's acquisition alive while side-effect removal discarded release,
causing a GPU timeout. The exact minimal geometry case passes after the change.

Buffer aliases are normalized to the allocation VA with a two-level directory,
one optional 32-KiB leaf per 64-MiB VA region. Sparse binds publish mappings
after their input waits; released/rebound VAs clear stale entries. Ordinary
loads/stores use the same mapping as atomics. A vector or misaligned scalar
crossing independently mapped pages is split into aligned pieces. Texture
image accesses retain their image VA; root/set/texture descriptor metadata is
excluded. No process-local VA is embedded in cached shader code.

Independent probes found issues beyond the atomic CTS group: separate atomic
counter copies across VAs, stale ordinary reads after atomic writes (and vice
versa), an image-atomic regression from remapping image VAs, and unnecessary
texture-descriptor lookups that exposed a sampler register-allocation failure.
Those failed experiments remain in the evidence record, followed by corrected
runs. No failing result was reclassified as a pass.

## Validation

All M4 experiments use watchdog 60 s, six thermal channels sampled every 0.5 s,
95 C cutoff, 12 CTS workers and unchanged 60 s per-case bounds. Requested full CPU
clock caps do not disable the CPU thermal policy.

- Full 256 buffer atomic64 cases: 192 Pass / 64 NotSupported (mesh/task), zero failures.
- Combined 1257 buffer+regression: 1007 Pass / 250 NotSupported, zero failures.
- 59 independent configurations: ten operations/returned values, nine ordinary
  counters, nine alias counters, sixteen mixed ordinary/atomic/reversed-page
  cases, six unsigned min/max contentions and nine 32-bit locking prototypes.
- Default 295 retained sparse and sampler configurations pass, including host
  timeline-wait sparse binds and page rebindings.
- Host/ARM64 builds pass. Host suite 104 Pass/same 11 known failures. Sparse NIR
  checks cover byte/vector masks, bound/unbound atomics, alias normalization,
  misaligned page-crossing loads/stores and descriptor versus application data.
- Default full sparse 14,319: 8,562 Pass / 5,757 NotSupported, zero failures,
  peak 71 C (`20260916T140917735974Z-dx-atomic64-default-sparse-full`).
- Default sparse/sampler 295 independent configurations: all Pass, peak 65.25 C
  (`20260916T135940346419Z-dx-atomic64-default-oracles`).
- Final default buffer/regression 1,257: 1,007 Pass / 250 NotSupported, zero failures,
  peak 62.75 C (`20260916T141746742905Z-dx-atomic64-default-regression`).
- Final default 59 atomic/alias configurations all Pass, peak 60.25 C
  (`20260916T141904389100Z-dx-atomic64-final-oracles`).

A combined 15,576-case batch reached 4,004 Pass / 2,499 NS before the command-pool trim
case hit its 60-second limit under sustained sparse-worker contention. Other workers
were terminated by the runner, not independently diagnosed as crashes. The
same trim case passes alone in 4.8 seconds under the unchanged limit. Final sparse and
buffer/regression selections are executed separately with 12 workers; all catalog
identities and selected coverage are retained.

## Reproduction

`atomic64-buffer-cases.py NEW_DIRECTORY` generates ten independent operation
and returned-value tests. Its `shared` option prepares separate shared-memory
probes; it does not enable that driver feature. `atomic64-lock-cases.py
NEW_DIRECTORY [native] [alias|shared]` generates nine carry/sign/wrap histograms under
contention; `shared` requests per-workgroup counters. Without either `native`
or `shared`, it tests a 32-bit locking prototype.
`buffer-alias-cases.py NEW_DIRECTORY` generates sixteen 32/64-bit mixed-access
and reversed-page probes. Run generated `run.py OUTPUT ORACLE` only under the
mandatory guard and target Vulkan environment.

The C oracle accepts M4_ORACLE_ATOMIC64=1 (or shared for a separate shared
feature request), M4_ORACLE_ALIAS_FULL_VIEWS=1 and
M4_ORACLE_ALIAS_REVERSE_PAGES=1. Alias options require sparse alias mode. The
oracle compares every allocation byte, including guards.

`cts-bulk.py` now rejects inconsistent catalog metadata locally before staging
or arming a hardware run. Its mirror also retains the existing CPU-cap and
recorded environment options used during qualification. The first final-run setup had an old 1313-entry sidecar
for a 15576-case list and stopped before running a test. Its evidence is retained;
a new matching frozen selection is used for qualification.

Final qualified library SHA256:
`3754044c611f814cb3fac3eb64a8da84ce1efe124a72f5e054edf5184d7bc5cb`.
Full sparse and 295-oracle runs used
`d7563a9dd21d89de38b2fcfe0103f34a9956ad3ca0bb28b67142c5b975bf2f7a`;
the final build adds an explicit host-allocation-failure error path and a lock
comment clarification, with unchanged shader behavior. Archive qualification.json
records the individual runs and hashes. The qualified oracle source includes
preexisting root-tool 16-bit feature options absent from the ordinary mirror;
its exact source and binary hash are retained separately.

The frozen catalog remains
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
The rebuilt index has 736 groups / 1,664,734 non-Pass cases; 192 cases become Pass.
No shared/image64 support or Steam application validation is claimed.
