# Vulkan CTS random sample, seed 20260920

Frozen selection: 1,000 unique cases drawn uniformly without replacement from
226,262 Linux-applicable cases in the pinned Vulkan CTS 1.0.2.6 must-pass list.
Only Windows-handle/WSI and Android WSI cases are excluded before sampling;
optional capabilities and prior results do not affect selection.

This sample overlaps the prior seed 20260919 in four cases (996 new cases).
The overlap is retained rather than redrawing after selection. `selection.json`
records the seed, population, exclusions and source-list hash;
`previous-overlap.json` records those four case names.

Run configuration: 20 concurrent CTS processes inside bounded watchdog/thermal
guards, using the same installed native M4 Mesa and the separately identified
`deqp-vk-input-aspects-tess-chroma` historical CTS variant. Results and timing
will be retained here separately from all earlier samples. This random sample
is not a full Vulkan conformance result.
