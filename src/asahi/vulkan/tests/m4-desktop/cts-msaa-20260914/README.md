# M4 multisampled storage images — 2026-09-14

Native M4 G16G now advertises `shaderStorageImageMultisample` and 1x/2x/4x
storage-image sample counts. Optimal 2D images, arrays and single-layer array
views use native per-sample reads and GPU stores through the physical tiled
layout's linear PBE alias. No CPU rendering or sample emulation was added.
Attachment usage retains the separately qualified render-pass format limits;
this change does not enable additional render-target formats or higher counts.

## Hardware qualification

| Group | Pass | NotSupported | Fail/crash |
| --- | ---: | ---: | ---: |
| Original CTS 2x/4x storage-image load/store group | 78 | 0 | 0 |
| Same cases, formatless stores | 78 | 0 | 0 |
| Same cases, formatless stores and coherent/volatile loads | 78 | 0 | 0 |
| Original render/resolve controls and high-count limits | 12 | 2 | 0 |

The 78-case list exhausts this release's 2x/4x storage-image group: 13 formats,
three view arrangements and two counts. The two shader variants retain the
same input data and CPU/checksum oracle; they are additional diagnostics,
not 156 new official CTS cases. Their exact patches, binary SHA256s and
source-restoration manifests are retained here. The standard binary is the
previously documented `deqp-vk-scaled-border-reference`; its unrelated historical
format/reference corrections do not change the multisample test source.

Each final case ran once on J713 with the 60-second watchdog lease, enabled CPU
thermal policy, six PMP temperature channels sampled every 0.5 seconds and a
75 C cutoff. Peak recorded temperature was 59.25 C. No guard fault, device loss
or reboot occurred. The four groups took about 11.03 seconds of batch execution
in total, excluding installation and host-side evidence collection. Full
commands, results, exact Mesa/CTS hashes, boot ID and experiment paths are in
`final-*-{results,summary}.json`; watchdog and thermal evidence is in
`guards.json`. The compiled candidate includes compiler fixes committed as
`098248c05fe` and the capability changes committed with this report. Original
experiment directories retain the exact pre-commit source diff and binaries.
These focused results do not claim Vulkan conformance or a new application test.

## Fixes

- Install real MS storage descriptors, route unqualified image reads to native
  MS texture fetches, and preserve coherent/volatile reads as memory intrinsics.
- Use M4 PACK_SMPIR sample/layer packing for both image loads and texture fetches.
  Preserve NIR constant-index metadata when changing the intrinsic kind.
- Keep constant sample/layer inputs in registers: PACK_SMPIR has no corresponding
  immediate input form. The existing immediate packed-layer zero remains valid.
- For formatless array stores, derive bytes per sample from the linear PBE alias
  before converting the layer byte stride to sample indices. PIPE_FORMAT_NONE's
  generic block size is one, so explicitly signal that the real size is dynamic.
- Accept formatless integer stores through the native raw-integer IMGWCI path.
  Its input is four 32-bit lanes; the PBE supplies destination channel widths.

`development-results.json` retains failed candidates and evidence paths. The
register-bank experiments did not fix the coherent-load issue and were removed.
The final code retains the existing staging allocation and synchronization.

## Original random-sample limits

Original seed20260920 case 526 requests 64 samples:
`dEQP-VK.rasterization.interpolation_multisample_64_bit.lines`.
Original case 889 requests 32 samples:
`dEQP-VK.image.load_store_multisample.2d_array.r32g32b32a32_sfloat_single_layer.samples_32`.
Both remain NotSupported. Case 889 now reaches the requested-sample-count check
instead of stopping at the missing storage-image feature. This work does not
reduce the original 1,000-case sample's remaining 62 skips.

Apple's [Metal feature tables](https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf)
list a maximum of four samples per render pass for Apple9. The
[Apple9 family documentation](https://developer.apple.com/documentation/metal/mtlgpufamily/apple9)
includes M4. No 8x/32x/64x support is advertised, and Apple10/Neo evidence is not
used as M4 qualification. Higher-count storage-only emulation is outside this
native implementation.

## Reproduction

Copy `cases.txt` to `/root/cts-1.0.2.6/msaa-storage-2x4x.txt`, then from the
workspace root run:

```sh
python3 tools/cts-campaign.py --name msaa-storage --jobs 10 --count 78 \
  --cases /root/cts-1.0.2.6/msaa-storage-2x4x.txt \
  --binary /root/cts-1.0.2.6/deqp-vk-scaled-border-reference \
  --output build/cts-msaa/new-standard
```

Build diagnostic executables with `tools/cts-patched-build.py` and the retained
patches, then use the same guarded case list with those separately named
binaries. Keep diagnostic reports separate from the original CTS report.
