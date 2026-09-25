# M3 integer64 storage images (2026-09-25)

G15S now exposes VK_EXT_shader_image_atomic_int64, including sparse atomics,
using the existing G15/G16 ABI implementation. R64_UINT/SINT optimal images
and storage texel buffers use RG32_UINT physical formats. Both texture and
PBE view descriptors must use the physical format, while the image-atomic
address calculation retains its logical eight-byte texel. Shader loads/stores,
striped device locks and sparse residency lowering were already shared.

The implementation changes format/feature admission and view construction;
it does not introduce a second compiler path or change M4's behavior.

Initial M3 hardware validation: 16 ordinary operation/returned-value/contention
probes, 64 sparse signed/unsigned probes across all four residency masks, and
8 formatless image/texel-buffer probes all pass independent expected bytes.
The contention probes cover up to 1,024 updates to one texel. Partial sparse
probes check zero reads and discarded writes in unmapped tiles.

Candidate library SHA256:
2340b16958976498aeb154f7bba4081a333acd5825cdbe010316e2935e6a3f04

Evidence: workspace build/m3-image64-20260925/{ordinary-r3,sparse-r1,formatless-r1}.
Peaks 48.75, 49.5 and 53.5 C respectively. Every run verified the 60-second
watchdog, retained CPU thermal policy, sampled all enabled PMP channels every
0.5 seconds, and used a bounded 45-second guard (no watchdog renewal).
The initial two attempts failed before GPU submission because the oracle
selected M4 only; qualified-oracle.c now explicitly accepts M4_ORACLE_DEVICE=M3
with chip ID 0x6030, retaining the M4 default and software reference mode.

Generate each corpus with its included generator. Run image64-run.py as a
direct child of the M3 guard, with CASE_DIR ORACLE OUTPUT [START [COUNT]].
The runner records exact input hashes, returned bytes and per-case outcomes;
ordinary failures stop for review, while kernel faults retain recovery.
Use ASAHI_M3_EXPERIMENTAL=1, M4_ORACLE_DEVICE=M3, the candidate VK_DRIVER_FILES
and LD_LIBRARY_PATH. Qualification used AGX_MESA_DEBUG=nopromote and
HK_DEBUG=nocompress, matching the existing replay configuration.

CTS and Satisfactory replay qualification are in progress; this initial
checkpoint does not claim full extension conformance or game replay success.
