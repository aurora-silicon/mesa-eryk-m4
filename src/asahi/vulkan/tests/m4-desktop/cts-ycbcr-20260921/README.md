# YCbCr extended CTS coverage — seed 20260921

The original seed-20260920 sample has four applicable packed conversion/format
cases passing; its remaining three YCbCr skips request DISJOINT on single-plane
formats. They cannot legitimately become supported by setting that feature bit.

Expand coverage using the original Vulkan CTS 1.0.2.6 mustpass list. The new
1,000-case selection is deterministic: random.Random(20260921).sample in category
order conversion600, format200, copy100, plane_view50, query50. No exclusions.
selection.json retains source hash, algorithm and exact cases.

Baseline: 827 Pass / 172 NotSupported / 1 Fail / zero crashes. The real failure,
index 987, is a packed 10-bit textureQueryLod. It reports a horizontal footprint
one mip level too small because the hardware descriptor measures two-pixel
words. Double only the query's horizontal coordinate derivative, retaining the
hardware LOD calculation and clamps. This also handles internally generated
queries used to select minification versus magnification filtering.

All eight packed-format LOD probes now pass. The complete packed 4:2:2 group
then ran once: 1,016 Pass / 544 NotSupported / zero failures or crashes across
1,560 cases. Selection is all dEQP-VK.ycbcr entries containing _422_unorm and
not containing plane; packed-all.txt retains the exact list in mustpass order.
Coverage includes 8/10/12/16-bit words in both orders; conversion, reconstruction,
vertex/fragment/geometry/tessellation/compute sampling, raw transfers and queries.
All 544 skips explicitly request disjoint single-plane storage (448 conversion,
96 copy). No single-plane DISJOINT support is advertised.

The expanded random baseline's other 97 non-disjoint skips request YCbCr
conversion for ordinary RGB/plane-view component formats; those optional
conversions are not implemented here. Its remaining 75 skips are single-plane
disjoint requests. Do not describe this as complete YCbCr or Vulkan conformance.
The random baseline report is unchanged; lod-results.json and the full packed
rerun establish the repair, rather than silently replacing its failing entry.

Hardware execution used 20 workers, 30-second per-case bounds, the 60-second
watchdog lease and six-channel 0.5-second thermal monitoring with 75 C cutoff.
Exact installed library/CTS hashes, timing, boot IDs and evidence locations are
in each summary. The focused guard also includes 89 image-format checks,
recorded under ../cts-seed20260920-1000/formats, outside this YCbCr pass count.
YCbCr tests use the existing qualified CTS corrections; the newly corrected
scaled sampler/border test paths do not change YCbCr tests or their oracles.

During idle work the known hourly DCP callback fault latched the display at
22:43:44 UTC. Evidence was saved in build/cts-ycbcr-extended/display-recovery;
guarded reboot restored SDDM and cleared the failed-flip counter. The final
packed tests ran on boot ff2548a7-3200-47a3-b3e9-ffd5c0ec52ea. This YCbCr fix
does not claim to repair the independent DCP callback failure.
