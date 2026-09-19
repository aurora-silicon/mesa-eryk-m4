# Private image clear batching

HK_M4_BATCH_DISJOINT_CLEARS=true (default false) batches adjacent M4 compute
streams consisting entirely of clears into one ordered stream. It crosses a
memory-dependency boundary only when complete, whole-plane canonical ranges
(including compression metadata) are disjoint. Imported, exportable and sparse
storage, unknown dispatches, query resets and timestamps keep their boundaries.
The final firmware completion still publishes all writes before later readers.
No barrier is simply removed from an arbitrary application compute shader.

The per-stream clear dispatch count establishes that the destination ledger is
complete only at EndCommandBuffer. Previously the universally incomplete
compute ledger prevented even known independent clears from merging.

Host hk_drm_render_core checks pass for adjacent ranges, real overlaps, later
members of a merged footprint and incomplete footprints. A stale 256-byte UAPI
size assertion is corrected to the existing 320-byte timestamp-copy ABI.
Native ARM64 Mesa builds successfully.

Hardware evidence (60-second watchdog, six sensors, 95 C cutoff):
- 20260919T044315514885Z-disjoint-clear-batch-readback: exact pixels but no
  actual batching; the incomplete ledger still blocked merging. Not a batching
  qualification.
- 20260919T044504973505Z-disjoint-clear-complete-ledger: all bytes correct,
  four independent clears become one four-dispatch stream (merged=3), and a
  repeated overlapping set remains two streams. Compressed and uncompressed
  images both pass; actual compression is audited. Peak 63 C.
- 20260919T044532052886Z-factorio-clear-ledger-frame: exact retained Factorio
  frame, zero differing pixels and identical PNG SHA256. Peak 87.25 C.

Workspace tools/vulkan-clear-batch-probe.c is the reusable reproducer (mirrored
here). Build with cc -O2 -Wall SOURCE -lvulkan -o probe on J713. Run only under
tools/gpu-experiment.py with the batching option, ASAHI_M4_EXPERIMENTAL=1,
VK_DRIVER_FILES=/opt/asahi-m4-port/asahi_icd.json, ASAHI_MESA_DEBUG=,
HK_M4_COMPRESSED_2D=true, HK_M4_COMPRESSED_FEEDBACK=true,
HK_M4_COMPRESSION_AUDIT=true and HK_COMMAND_PROFILE=true. The extra rounds
bind separate alias images to one allocation and check the last written alias.

Live FPS qualification is pending; full CTS remains deferred.
