# September 18 Vulkan CTS random-sample fixes

Original sample: 10,000 frozen Vulkan 1.4.6.2 cases, seed 2026091801,
catalog SHA256 72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.
Original results and every interrupted attempt remain in
`build/cts-20260918-reboot-10k/`; this follow-up does not replace those outcomes.

## Descriptor pool padding

`pipeline.monolithic.descriptor_limits.compute_shader.samplers_1024` failed
allocation with VK_ERROR_OUT_OF_POOL_MEMORY. Gen9 sampled descriptors occupy
76 bytes, but distinct bindings require 8-byte alignment, yielding 80-byte
spacing. Pool sizing reserved only 76 bytes per descriptor. Round each pool
entry stride up to the pool's maximum binding alignment, with 64-bit arithmetic,
to cover arbitrary layouts without changing the descriptor ABI.

## Sparse binding format queries

`api.info.image_format_properties.1d.optimal.r8g8b8_srgb` rejected required
SPARSE_BINDING combinations because the format has a non-power-of-two block
size. The restriction belongs to sparse residency, which uses texel tiles;
opaque sparse binding uses byte/page ranges and does not require residency
queries. Restrict the rejection to SPARSE_RESIDENCY, preserving all residency
and host-copy restrictions.

## Validation on M4 J713

Both original cases pass under the watchdog and thermal guard:
`20260918T071131028000Z-cts-fixes-api`.
All 414 selected monolithic descriptor-limit and RGB image-format query cases
pass: `20260918T071255781618Z-cts-fixes-api-neighbors`, peak 61.5 C,
16.092 seconds of runner execution. Lists, indexed metadata, raw QPA and
library identities are retained under `build/cts-fixes-20260918/` and the
corresponding `_provenance/experiments/` directories.

The memory-model device loss was still outstanding at this checkpoint; its fix
and final validation are recorded below.

## Register allocation at phi nodes

`ssbo.phys.layout.random.8bit.vector_types.21` aborted in RA validation.
The allocator moved phi destinations one copy at a time. A later move could
then match an already moved phi, losing the original parallel-copy mapping.
Match all copies against each original phi before inserting live-in phis;
handle both SSA and register-only destinations, and skip unallocated phis.

The captured compiler input is retained in `ra-shuffle-input/`. From the
workspace root, build `build/mesa-m4-asahi-host/src/asahi/compiler/agx_shader_replay`
and use `tools/compiler-replay.py <this-directory>/ra-shuffle-input --output NEW`.
The baseline aborts with invalid RA sources; the fixed replay exits zero.
The original hardware case passes, as do all 25 physical 8-bit random vector
SSBO cases on the final library: `20260918T071557271496Z-cts-fixes-ra-final`.
Peak 60.75 C; 2.827 seconds runner time. The 22 LowerParallelCopy, LowerSpill,
and RepairSSA unit tests pass, along with the static spill core test.

A broader `agx_tests` invocation encountered stale expected shuffle registers
and an assertion while printing an intentionally invalid packer operand;
those tests do not exercise this RA path. Their output is retained in
`build/cts-fixes-20260918/compiler-unit-tests.log`; no broad-unit-suite pass
is claimed.

## Shared atomic64 progress and barrier scope

The original device loss was
`memory_model.write_after_read.ext.u64.noncoherent.atomic_atomic.atomicrmw.subgroup.payload_nonlocal.workgroup.guard_nonlocal.workgroup.comp`
(global catalog index 955418). It timed out a GPU job after two seconds with
one CTS worker, so this was not the runner's 60-second case bound or parallel
CTS contention. The shader dispatches 64 workgroups of 31 x 31 invocations.

Shared 64-bit atomics used the device-wide hashed lock table intended for
buffer/image atomics. Independent workgroups therefore contended on identical
shared offsets. A reduced two-exchange oracle passed with one workgroup and
hung with 64. Bounded diagnostics recorded exhausted retries and the owning
workgroup/invocation; stalled workgroups were waiting on other workgroups.
The same shader now passes using a lock in its own workgroup's shared storage.
This removes that unnecessary inter-workgroup progress dependency.

Reserve one private 32-bit shared lock word, initialize it in invocation zero,
and execute a workgroup barrier before application control flow. Invocations
that return early still participate in initialization. Acquire/update/release
remain inside the retry loop so losing lanes never wait for a winner that has
already left their subgroup's active mask. All shared atomic64 operations use
this lock, and their fences now name shared memory at workgroup scope.
Buffer/image atomic64 retain their existing device-wide locks.

Memory-only and subgroup fences also incorrectly selected workgroup execution
rendezvous for shared/image memory. Use the non-rendezvous device fence for
shared ordering/retirement and the native texture fence pair for image memory;
retain full rendezvous for workgroup execution barriers. The compiler unit
checks distinguish these scopes. `barrier-witnesses/` retains Metal 917 sources,
M4-target binary sections, and compiler manifests; these are offline encoding
witnesses, not macOS GPU validation.

The private word does not reduce the public 32-KiB shared-memory limit. Allow
one additional 64-byte allocation granule in the internal launch record.
Independent probes fill and verify all 4096 application uint64 words while
using atomic exchanges, across 64 workgroups at both 256 and 1024 threads.
The full 32-KiB application allocation and private lock remain disjoint.

Temporary retry limits, owner tracking, alternative unlocks, and register-hint
experiments are removed. Native atomic wait-field contrasts did not fix the
hang; that encoder was restored unchanged. Their failed experiments and source
patches remain in `_provenance/experiments/`.

## Final validation

Native M4 library SHA256 at qualification:
`413ca42d87c22b3e0ab49558ca3c801c889bd99f74cc19076aa0db83594a4706`.
All runs use the 60-second watchdog, six thermal sensors at 0.5-second cadence,
95 C cutoff, and unchanged 60-second CTS case limit.

- All four original failures now Pass, one worker, 3.03 seconds runner time,
  peak 60.75 C: `20260918T081641181858Z-cts-fixes-original-four`.
- Atomic and neighboring shared-ordering CTS: 438 completed, 326 Pass /
  112 NotSupported, no failures, crashes or timeouts; two workers, 68.98 seconds,
  peak 64.5 C: `20260918T081252536057Z-cts-fixes-atomic-neighbors`.
- Four retained independent oracles all Pass, peak 59.5 C:
  `20260918T081510417157Z-cts-atomic64-final-oracles`. These cover partial-subgroup
  peer progress, Vulkan read-before-write ordering, and full shared capacity at
  256 and 1024 threads per workgroup.
- Host `agx3_workgroup`, `agx3_pack`, and `asahi-agx-ioto-state-core` all Pass.
  ARM64 driver build passes. Earlier RA/API neighbor results remain above.

`shared-atomic64-ordering.py NEW_DIRECTORY` in the parent directory (and workspace
`tools/`) generates and SPIR-V-validates the four oracles. Build the updated
`vk-buffer-oracle.c`; the ordering case requests
`M4_ORACLE_VULKAN_MEMORY_MODEL=1`. Run generated `run.py OUTPUT ORACLE` through
`gpu-experiment.py` and `run-m4-gl.py`, preserving the mandatory guards.
The frozen, indexed CTS selections and result summaries are retained here;
raw QPA and all failed attempts remain in the experiment directories.

This repairs the four failures from the 10k sample; it is not a new complete
Vulkan conformance run. TODO: extend large-workgroup forward-progress coverage
to the separate device-wide buffer/image atomic64 locking path. The existing
buffer atomic64 regression passes, but the shared-memory fix does not qualify
all possible contention patterns in that path.

## Deployment of committed fix

Commit `95a0f3b7c9d` was rebuilt and installed as native M4 library SHA256
`d8bb9362854c5b1cea505d34a5bd92a16d86510fcfebdbf70d4e2833762f3ea0`.
The four original cases pass again on that installed build:
`20260918T082055751402Z-cts-fixes-original-four-deployed`, peak 61 C.

The normal autologin desktop was restarted to release its diagnostic library
mapping. Hyprland's mapped library inode matches the final installed library;
eDP-1 is 2560x1664@60 and the Philips DP-1 is 1920x1080@60:
`20260918T082043963915Z-cts-fixes-final-desktop-verified`. SDDM is active,
the watchdog is armed/idle at 60 seconds, and no CTS worker remains.
The first activation-check script used the wrong hyprctl path and reported a
process failure; the desktop itself had started correctly. The corrected
read-only check uses `/usr/bin/hyprctl` and passes without another restart.
