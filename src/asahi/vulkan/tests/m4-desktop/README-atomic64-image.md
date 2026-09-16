# M4 integer image atomic64 (2026-09-16)

G16G implements and advertises EXT_shader_image_atomic_int64 and
shaderImageInt64Atomics for optimal R64_UINT/SINT storage images and storage
texel buffers. Physical RG32_UINT layouts/descriptors preserve the eight texel
bytes. Shader loads pack the two words and supply logical y/z=0,w=1; stores
unpack logical x. Atomics retain their logical format until address lowering,
then use the qualified native32 critical-section implementation.

Atomic64 now uses 2,048 device-wide locks selected by the aligned eight-byte
word's offset within a 16-KiB GPU page. Page offsets agree across physical
aliases, including RO VAs and resource offsets within VkDeviceMemory. Unrelated
texels can progress independently. Shared addresses may collide between
workgroups safely. This uses 8 KiB of the existing 16-KiB lock allocation.
The buffer/shared reports describe the earlier single-lock implementation;
this change supersedes that locking policy while retaining its semantics.

The initial 32-case image probe exceeded the kernel's two-second GPU retirement
limit with one global lock (job23736, firmware snapshot retained). Watchdog
recovery restored normal login. The same 32 cases pass with striped locks in
1.826 seconds at 12 workers, with no timeout increase. A separate sparse-read
compiler validation failure came from a five-component data/residency result
incorrectly marked as four components; the conversion now retains five.
Coordinated worker interruptions in failed runs are not independent crashes.

## Validation

Every M4 run uses the 60-second watchdog, six thermal channels at 0.5 seconds,
95 C cutoff, 12 CTS workers and unchanged 60-second case bound. CPU thermal policy
remains enabled; requested full-clock caps are only ceilings.

- Full 18,789-case extension group: 5,510 Pass / 13,279 NotSupported, zero failures,
  peak 71.25 C: 20260916T145033617843Z-dx-image64-cts-full-r1.
- Additional 4,214 catalog cases naming R64 formats: 1,328 Pass / 2,886 NotSupported,
  zero failures, peak 66.75 C: 20260916T150154559571Z-dx-image64-cts-extra-r2.
- 100 independent buffer/shared/image/alias/private-scratch configurations pass,
  including image operations and returned values, load/store visibility,
  fully-backed image residency, and 64/256/1024 invocation return histograms:
  20260916T150358899560Z-dx-image64-all-oracles-r3, peak 64 C.
- Default 1,356-case buffer/shared/image regression: 1,060 Pass / 296 NotSupported,
  no failures, peak 64.5 C: 20260916T150610863215Z-dx-image64-default-regression.
- Final default 100 independent configurations pass, peak 62.25 C:
  20260916T150750367300Z-dx-image64-default-oracles.
- Host and ARM64 builds pass. Host suite 104 Pass with the same 11 known failures.

The additional format run covers clears and copies as well as sparse-read
instructions on ordinary images. The first full group ran before the sparse
result-count correction; all sparse-read compiler failures and the extra group
were rerun with that correction. Exact hashes, frozen indices, diagnostics,
reference inputs and results are retained under atomic64-image/.

Software reference: all 15 operation/load-store/contention probes pass lavapipe.
The added 16th probe expects every fully-backed texel to be resident; its payload
passes on lavapipe but 64 of 256 residency booleans are false. That discrepancy is
retained in image-reference-r3 and has not been fixed in the reference driver.
M4 passes all 16 against the independent expected bytes. It is not reported as
an all-pass lavapipe run.

`atomic64-image-cases.py NEW_DIRECTORY` generates the 16 image probes. Run its
`run.py OUTPUT ORACLE` only under the mandatory hardware guard. The raw-buffer
oracle adds M4_ORACLE_IMAGE_ATOMIC64=1; ordinary storage-image mode no longer
requires sparse bindings, and R64 image-upload offsets use eight-byte texels.
The qualified source preserves the root tool's preexisting 16-bit options,
which are absent from its ordinary Mesa mirror.

Still unadvertised: sparseImageInt64Atomics, linear image atomic64, sampled R64,
and R64 formatless reads/writes. Skips also include ray tracing, exclusive compute
queues, R64 vertex formats and long-vector/indexing features. These are separate
pending capabilities, not successful tests. Full DirectX/Steam work continues.

Frozen catalog SHA256:
72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.

Qualified default library SHA256:
bd34220a20e486893e6dcb1f10498f7ebea60e79984db0ea33bd6e066a5ce7ad.
