# M4 shader resource residency (2026-09-16)

G16G implements shaderResourceResidency with native sparse SMP operations for
images and live binding metadata for texel buffers. This extends the previously
qualified sparse binding and alias support; strict nonresident behavior and
shaderResourceMinLod are separate remaining features.

## Implementation

Apple's offline G16G compiler sets SMP field 142 to request a residency word,
following the color result; bit zero indicates nonresidency. Byte-exact witnesses
cover 2D/array/3D reads, register LOD, samples, gathers, depth comparisons and
comparison gathers. Native witness field 112=8 belongs to Apple's extended
resource descriptor path. Honeykrisp retains its native heap selector 4: copying
8 caused false nonresident results and atomic sparse-read failures. These fields
are independent in the encoder. The witness compiler ran remotely on a Mac;
the hardware qualification below ran on M4 J713.

Sparse depth comparison writes one comparison value followed by residency.
The compiler retains the conservative full sparse destination clobber span,
extracts the two logical results, and excludes residency from the color mask.
Cube projection accepts sparse results. Scaled-format conversion and
custom-border arithmetic preserve the residency code separately.

Unbound linear texel buffers use real zero/scratch pages and cannot obtain
virtual binding status from the texture hardware. Each sparse buffer therefore
owns one uint32 per 16 KiB page. Successful queue binds update this map. Gen9
texel descriptors carry its address, the view's page offset, texel size and
extent; normal descriptors have a null map and are resident. Shaders query
both pages touched by a texel, including RGB32 crossing a page boundary. The
byte-offset calculation is 64-bit for maximum-sized RGBA32 views crossing 4 GiB.
Descriptor copies retain a live map pointer across queue rebinds. Texel descriptor
stride grows from 72 to 96 bytes; mutable/push descriptor storage accommodates it.
Sparse bind ioctl failures are now propagated instead of silently ignored.

No multisample sparse residency feature is advertised. A valid multisample
image access is fully bound, so sparse instructions return the ordinary sample
and a resident code. This includes sparse-binding-only multisample resources.

## Validation and reproduction

Every hardware run uses the mandatory 60-second watchdog and six-channel
0.5-second thermal monitoring with the 95 C cutoff. CTS uses 12 workers and
an unchanged 60-second per-case bound. Frozen catalog SHA256:
`72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf`.

Initial qualification exposed and fixed 664 image failures (wrong selector and
unsupported 3D read shape), then four texel-buffer residency failures (valid
scratch mappings). Full sparse qualification: 8,507 Pass / 5,812 NotSupported, zero failures
across all 14,319 non-device-group sparse_resources cases (451 s, peak 69 C).
Final qualification records are in `sparse-residency/`.
Complementary validation: 3,963 Pass / 2,238 NotSupported across 6,201 cases
(image atomics, buffer queries, 2,400 descriptor-offset cases and the
1,001-case regression selection), zero failures, peak 68 C. All 32 buffer
oracles and six image oracles Pass.
Host and ARM64 builds pass; the Asahi host suite retains 103 passes and its
11 pre-existing failures. Texture-core byte-exact tests pass 6,398 checks.

The adjacent `sparse-residency-cases.py NEW_DIRECTORY` creates 32 independent
buffer cases, with uniform/storage formats, RGB32, partial maps, view offsets,
rebinds without descriptor updates, and a virtual range crossing 4 GiB backed
by only a small allocation. `sparse-image-residency-cases.py NEW_DIRECTORY`
creates six image oracles: D32 compare/gather, fully-bound MS4 R32UI,
R32F cube level/gather and R8_USCALED level sampling. Both generators
compile and validate SPIR-V and preserve independent expected readback bytes.
The image cases use fully-bound images; partially-bound images are covered by
CTS. GLSL has no sparse buffer overloads, so the buffer generator rewrites an
ordinary image operation to its sparse SPIR-V form and retains the assembly.

Build adjacent `vk-buffer-oracle.c` for ARM64 with the Vulkan headers and `-ldl`.
Copy the generated cases and run `run.py OUTPUT ORACLE` only through workspace
`tools/gpu-experiment.py` and `/root/run-m4-gl.py`. The library needs no feature
experiment switch. The buffer oracle accepts `M4_ORACLE_SPARSE_TEXEL_MASK`,
`M4_ORACLE_SPARSE_REBIND_MASK`, and `M4_ORACLE_SPARSE_HIGH_OFFSET`; the latter
maps a small backing window at a high sparse virtual address.

Full evidence is under workspace `_provenance/experiments/`:

- `20260916T080336478861Z-dx-residency-all-r1`: initial selector/3D failures.
- `20260916T080750579116Z-dx-residency-selector4`: all 664 image failures Pass.
- `20260916T081035883376Z-dx-residency-all-r2`: only four buffer failures remain.
- `20260916T081857826879Z-dx-residency-buffer-r1`: six buffer queries Pass;
  12 cases need separate strict/int64 features.
- `20260916T082748509854Z-dx-residency-oracles33`: 30 buffer probes Pass;
  depth compiler refusals retained as diagnostic evidence.
- `20260916T083447234116Z-dx-residency-depth-r3`: both depth probes and MS Pass.
- `20260916T083609112883Z-dx-residency-full-final`: full sparse qualification.
- `20260916T084404639528Z-dx-residency-wide-oracles`: all 32 buffer oracles Pass.
- `20260916T084437866694Z-dx-residency-complement-final`: complementary CTS.
- `20260916T084850657098Z-dx-residency-image-complete`: six image oracles Pass.
- `20260916T084921495135Z-dx-residency-queries-final`: final 6,238 query cases,
  2,754 Pass / 3,484 NotSupported, no failures, 193 s, peak 67 C. This rerun
  includes the final cube and scaled-format changes and 64-bit buffer offsets.
