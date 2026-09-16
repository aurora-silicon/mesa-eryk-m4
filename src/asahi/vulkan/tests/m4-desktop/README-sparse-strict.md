# G16G strict sparse residency

G16G implements `residencyNonResidentStrict` with a device-wide bitmap of
primary virtual-address pages. Bound global reads retain their original VA,
preserving visibility of writes within an invocation. Unbound bytes are masked
to zero and unbound global/image atomics return zero without executing.
Ordinary stores to unbound memory reach the isolated scratch mapping; subsequent
reads cannot observe those writes. Native sparse image/texel fetches retain
their existing residency tables and format defaults.

The bitmap has one bit per 16 KiB page. Ordinary allocations start resident;
sparse allocation/binding/free update it under a CPU mutex. Shaders obtain its
address and VA bounds through the root descriptor, so disk-cached binaries
contain no process-specific map pointer. Read-only aliases normalize to their
primary address. Vector loads check each scalar, and misaligned scalars mask
each byte across page boundaries. Addresses outside the sparse VA window use
a safe bitmap index and retain their ordinary behavior.

Sparse CPU mapping and bitmap publication wait for incoming semaphores. The
first sparse-bind call enables the queue's submit thread, allowing a later host
signal to release a wait without blocking QueueBindSparse itself. Application
dependencies must still synchronize accesses against rebinding.

The shader pass applies only to G16G devices enabling partially resident sparse
resources. Proven root/descriptor-set metadata accesses skip the pass; an
application address loaded from metadata is checked when dereferenced.

## Qualification record

All hardware experiments use the 60 s watchdog, six-channel 0.5 s thermal
monitor, 95 C cutoff, 12 CTS workers and unchanged 60 s case bound. CPU maximum
requests do not override lower thermal-policy limits; elapsed times are not
full-clock performance claims. Frozen catalog SHA256:
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.
Original catalog indices and result records are retained under `sparse-strict/`.
The CTS executable is the 1.4.6.2 `deqp-vk-minmax-components` build documented in
`README-sampler-minmax.md`, SHA256
`c1a637217f05b42ef068ef2b777aadc8d70a3f44d20e5155ca6bbb31ad0a165f`.
Its sampler-admission/scaled-blit patches do not change sparse comparisons.

The initial property-only trial (`20260916T115427874281Z-dx-strict-gate-r1`)
produced 38 Fail / 17 Pass / 1 device-group skip: scratch writes contaminated
SSBO/BDA reads. This was an experimental build, not advertised support.
The bitmap implementation passed all 55 ordinary strict cases, with the same
one device-group skip (`20260916T120325219887Z-dx-strict-map-r1`, peak 63.25 C).
The full 14,319-case sparse family then gave **8,562 Pass / 5,757 NotSupported /
zero failures** (`20260916T120538511010Z-dx-strict-full-r1`, peak 69.5 C).
These runs used the temporary enable switch and library SHA256
`e71982462c774b334a7ca64924bc1ee5a88f7e36b4f0ecb11261f6413b91d5fa`.

After the CPU wait/thread fix, 24 independent image configurations pass
(`20260916T121436147226Z-dx-strict-image-r2`, peak 62 C), and all 24 pass with a
later host timeline signal releasing the sparse-bind wait
(`20260916T121504508222Z-dx-strict-image-wait-r1`, peak 62 C). This validates
completion and values without host deadlock; it does not independently measure
the instant of publication before the wait. Library SHA256:
`e87ab1e9f2bd2fdb3b5d0f95b4c18d01f736ee6f8b02fc943f04c1c67112f49f`.
The first image probe run had the wrong descriptor-write type for storage
images; its failed readbacks are retained, and the corrected probe produced
the results above without a driver change.

Host testing adds 208 byte/vector masks, bound/unbound atomic execution and
the descriptor-versus-application-pointer distinction. The Asahi suite gives
104 Pass and the same 11 known failures. ARM64 and host driver builds succeed.

The default-enabled build with the CPU wait/thread fix also passes the full
14,319-case family: **8,562 Pass / 5,757 NotSupported / zero failures**
(`20260916T121703843832Z-dx-strict-default-full`, peak 66.75 C), library SHA256
`15a958ca96a0ad50ce686308d815c830de4af836138e959ffedb5c057f6e6272`.

The final optimization skips descriptor metadata and devices without partially
resident resources. Library SHA256:
`cc19556c0758d27c13ee2b60977bad08245307ea96157abe25fb687079e6cc0d`.
The 2,318-case strict/minimum-LOD/subgroup/fixed-bug regression passes with
**1,930 Pass / 388 NotSupported / zero failures**
(`20260916T122659595231Z-dx-strict-final-regression`, peak 65.25 C).
All **295 independent configurations** pass on this build
(`20260916T122911556143Z-dx-strict-final-oracles`, peak 62.75 C): 24 image
configurations, the same 24 with host-signalled waits, 40 buffer configurations
and 207 existing sampler configurations. Buffers cover scalar/vector reads,
cross-page values, stores, atomic adds and store/atomic sequences across four
residency masks, with and without a warm-up dispatch followed by rebinding.
Every configuration compares the complete backing buffer against CPU-generated
data, including unaffected regions. The earlier standalone buffer run also
passed (`20260916T122623750693Z-dx-strict-buffer-r1`, peak 62 C).

These are CTS and independent API checks, not Steam application validation.

Final optimized-build full sparse qualification: **8,562 Pass / 5,757
NotSupported / zero failures** across all 14,319 selected cases, peak
64.75 C (`20260916T123029452068Z-dx-strict-final-full`).
