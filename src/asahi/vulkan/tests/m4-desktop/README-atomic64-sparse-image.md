# M4 sparse integer64 images (2026-09-16)

G16G implements sparseImageInt64Atomics for R64_UINT/SINT storage images.
Physical RG32 layouts retain eight-byte logical texels and the 128x64 standard
64-KiB sparse tile. Existing strict sparse mappings return zero and discard
writes on unbound tiles. Atomic64 locks still agree across VA aliases.

Independent qualification found an image read/write ordering bug: an image load
whose GPR result was consumed later could be overtaken by a store to that texel.
All six bound store probes returned the new value for the earlier read. G16G
image writes now drain outstanding async reads before publishing their own
staging work. Separate staging banks prevent register clobbering; they do not
order accesses to an aliased texel. The write still retains its own async slot
for subsequent image fences. A scoreboard test covers that new generation.

This applies to G16G image writes generally, including fully backed images;
it is not a sparse-case workaround. The native Apple read-before-write witness
and exact fetched fields are retained. Its IMGWCI includes an additional f114=3
whose meaning is not established here. We do not claim an explicit native
pre-store wait was decoded. Mesa uses its already-qualified explicit slot wait
and the hardware readback probes validate the resulting ordering.

Validation:
- First independent run: 58 Pass / 6 pre-store-value failures, retained as
  20260916T155937160480Z-dx-sparse-image64-oracles-r1.
- Fixed independent run: all 64 signed/unsigned load, sparse load, store,
  add, min, max, exchange and compare-swap configurations pass, across all
  four residency masks. 20260916T160526638178Z-dx-sparse-image64-oracles-r2.
- All 14 ordinary fully-backed operation references pass lavapipe. Reference
  mode excludes sparse-residency reporting because of the separately recorded
  lavapipe fully-backed residency discrepancy; it does not claim partial sparse
  hardware coverage. Expected bytes for all masks are computed independently.
- CTS probe: all 32 Pass, 2.241 runner seconds at 12 workers, peak 58.75 C:
  20260916T160604278186Z-dx-image64-sparse-probe-r1.
- Full 1,076 formerly blocked cases: 790 Pass / 286 NotSupported, no failures,
  peak 60.75 C: 20260916T160634146279Z-dx-image64-sparse-full-r1. Skips are 206
  device-group cases and 80 unsupported sparse image format configurations.
- Host suite: 104 Pass / same 11 known failures. Scoreboard core: all 75 checks.
- Default full sparse: 8,860 Pass / 5,459 NotSupported in 14,319 cases,
  no failures, 539.673 runner seconds, peak 62.25 C:
  20260916T174648927488Z-dx-image64-sparse-default-full.
- Combined default regression: 3,818 Pass / 3,318 NotSupported in 7,136 cases,
  no failures, 298.986 runner seconds, peak 62.25 C:
  20260916T175627390935Z-dx-image64-sparse-default-regression.
- Default final independent suite: all 172 atomic64, sparse, formatless, alias
  and scratch configurations Pass, peak 60.0 C:
  20260916T180143141701Z-dx-image64-sparse-default-oracles.
- Rebuilt missing index: 734 groups / 1,655,164 non-Pass cases (802 new passes).

The independent generator is atomic64-sparse-image-cases.py. The oracle requests
sparse image64 features for partial R64 images, accepts R32/R64 standard tiles,
and M4_ORACLE_IMAGE_EXTENT=WxH allows ordinary-image reference runs. Root and
Mesa oracle sources retain their preexisting int16-option difference.

All M4 execution remains guarded: 60-second watchdog, six thermal channels at
0.5 seconds, 95 C cutoff, 12 CTS workers and the unchanged 60-second case limit.
CPU thermal policy remains enabled. Frozen indices and failed records are kept.
Kernel 945d4f48a077 / evidence 353e76a9660f keeps private GEM reclamation bounded.

The full sparse run sampled peak Shmem 1959 MiB
and minimum MemAvailable 10646 MiB.

The combined run sampled peak Shmem 3390 MiB
and minimum MemAvailable 9052 MiB.
