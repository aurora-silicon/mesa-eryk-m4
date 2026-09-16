# M4 shared integer atomic64 (2026-09-16)

G16G now advertises shaderSharedInt64Atomics alongside the qualified buffer
feature. Shared add, signed/unsigned min/max, bitwise operations, exchange and
compare-and-swap use native 32-bit shared atomic halves under the existing
device-wide atomic64 lock. Winning lanes release before leaving the retry loop.
This conservative implementation serializes unrelated atomic64 operations;
correctness qualification does not establish application performance.

Apple Metal 917 offline witnesses support the ordering sequence: a shared-only
fence shader and its no-fence counterpart compile to identical bytes. The mixed
device-lock/shared-atomic witness emits the existing device fence at offsets
0x92 and 0xbe, with no workgroup execution rendezvous. Exact sources, compiler
manifests and binary sections are retained under atomic64-shared.

Large shared-structure CTS shaders exposed an atomic-return scratch allocation
bug: appending the return word after a 664-byte spill frame exceeded the
supported compact CLR address range. AGX3B shaders with used device-atomic
results now reserve the first 16 scratch bytes. NIR scratch addresses shift by
16 bytes and register allocator spills follow that adjusted frame. Constant
scratch offsets are adjusted directly; dynamic offsets use an integer add.
The alignment preserves existing vector accesses. Direct backend fixtures keep
the prior fallback when no reservation was requested.

## Validation

All hardware runs use the 60-second watchdog, six thermal channels sampled at
0.5 seconds, 95 C cutoff, 12 CTS workers and unchanged 60-second case bounds.
CPU thermal policy remains enabled; requested full-clock caps are only ceilings.

- Shared CTS: 16 Pass / 48 mesh-task NotSupported, no failures:
  20260916T143157871110Z-dx-atomic64-shared-cts-r2, peak 59.25 C.
- Default combined 1,321 CTS cases: 1,023 Pass / 298 unrelated NotSupported,
  no failures or timeouts, peak 60.25 C:
  20260916T143656387257Z-dx-atomic64-shared-default-regression.
- Six independent atomic-return/private-array/spill configurations pass on M4
  and lavapipe. Both normal and forced-spill modes check repeated scratch reads,
  dynamic writes and 64 live vectors:
  20260916T143542292187Z-dx-shared64-tps-oracles-r2, peak 58.25 C.
- Final default 84 independent buffer/shared/alias/scratch configurations pass,
  peak 58.25 C: 20260916T143815939494Z-dx-shared64-default-oracles.
- Host and ARM64 builds pass. Host suite: 104 Pass and the same 11 known failures.

The first shared CTS run had 16 pipeline-creation failures (no GPU fault).
The first atomic/private-array probe also failed pipeline creation: a constant
scratch offset became an unsupported two-immediate add. Both diagnostic runs
are retained; neither counts as hardware data corruption or a successful run.

`tps-oracle.py MODE --atomic-return --output NEW_DIRECTORY` extends the retained
scratch oracle with an atomic result live across private data accesses. MODE is
repeat, dynstore or spill; the latter accepts --vectors 64. Run its SPIR-V through
vk-buffer-oracle under the mandatory guard, then repeat with AGX_MESA_DEBUG=spill.
The tool mirror also incorporates the existing dynamic-store mode from the root
tool; that mode is exercised in both normal and forced-spill configurations.
The shared operation and contention generators are described in
README-atomic64-buffer.md. All probes compare exact reference bytes and guards.

The frozen Vulkan 1.4 catalog hash remains
72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.
Qualified default library SHA256:
e5bedfae7a9388d9a4ccb02c25890d584a734d981db44a57d9e3f49e4884299f.
Image atomic64 and the remaining DirectX capabilities are separate work;
no Steam application validation is claimed.
