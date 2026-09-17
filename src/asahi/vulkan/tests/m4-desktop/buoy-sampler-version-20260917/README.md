# M4 Buoy sampler versioning — 2026-09-17

Enable the guarded ordinary-sampler path by default on G16G. The preceding
commit 0d0ddaa2198 implements the transformation, fallback and host tests.
HK_M4_VERSION_SAMPLES=false remains an opt-out.

## Implementation and color correction

For bounded fragment shaders with loops and at least 16 eligible explicit-LOD
samples from one scalar combined sampler, choose between an ordinary-sampler
shader body and the complete emulation body using a uniform runtime check.
Min/max reduction, custom borders and view minimum LOD retain the fallback.
Descriptor arrays and binding flags are excluded. This does not change the
application's GLSL, resolution or texture.

The first prototype produced green/purple colors. Cloning branch-local output
stores violated the backend's single-exit output assumption. Lowering outputs
through temporaries and SSA restores one output store after the branches. The
host regression test checks this invariant. The corrected real application
again shows the red/yellow buoy.

## Measured application result

At 2560x1664, baseline steady FPS was 9.16–9.37; the corrected optimization
measured 14.71–14.72 FPS (approximately 60% faster). Heavy fragment p95 fell
from 92.403 ms to 50.505 ms (45.3% lower). This is not a 60 FPS claim.

Evidence under workspace _provenance/experiments/:

- 20260917T205506164515Z-buoy-random10k-fixes-installed: baseline.
- 20260917T212643527274Z-buoy-version-samples: rejected color-broken prototype.
- 20260917T211217305037Z-buoy-further-fixed-baseline: fixed-frame reference.
- 20260917T213031640341Z-buoy-version-fixed-colors: corrected fixed frame.
- 20260917T213154747290Z-buoy-version-colors-full: corrected full-resolution run.

The 640x360 fixed-time readbacks are not byte-identical: 1,479 pixels / 1,932
channels differ out of 921,600 channel bytes; mean absolute difference is
0.004199/255 and maximum 90/255. The sparse residual numerical differences
have not been fully explained. No blanket pixel-exact equivalence is claimed.

Full-resolution candidate v6 SHA256:
394a9939c56af2d050ece8ebca4a401ebae0cdf44164b31de99a32dbf4d98168.
Final default-enabled v7 removes unused experimental options. Its SHA256 is
f9fd7d2b6fabda42a7714b88438ca68b4c7e183feaac4e367a23a2ca3d4a937e.
V7 was tested through a private ICD; the global installed Vulkan library was
not replaced in this checkpoint. Existing desktop processes retain their driver.

## Completed 10,000-case sample

The campaign completed before the stop request could be applied. No additional
hardware tests were started after that request. All 10,000 unique selected cases
have results: 4,886 Pass, 5,114 NotSupported, zero failures, crashes, timeouts or
resource errors. Unsupported cases are not passes. This is sample coverage,
not full Vulkan conformance. Runner time: 599.62 s; total wall time: 670.43 s.

The unchanged random selection (seed 202609172000) uses the frozen 3,206,030-case
catalog. selection.json and compressed case metadata retain catalog indices;
results.json.gz contains every result. summary.json identifies all 11 guarded
segments and evidence locations. Ordinary segments used four workers; 91 WSI
cases used one worker and the 1000 Hz headless compositor (1 Pass, 90 unsupported).
All guards passed; maximum recorded temperature was 65.75 C. The 60-second
watchdog and 0.5-second thermal monitoring remained enabled.

CTS executable: /root/cts-1.4.6.2/deqp-vk-seed30-scaled-blit, SHA256
36bcc144d4b3c0edc5b638f20331fbc4e1be388d89da652890af870b8e5697dc.
This includes the previously documented CTS scaled-blit reference correction;
these results must not be attributed to the unmodified CTS executable.

Host sampler-selection/output and minmax-control-flow tests passed (logs here).
The sibling sampler-version-oracle.py prepares six focused fragment probes;
all six compile, but they have NOT been executed on M4. A separately adapted
Lavapipe fixture without image-view-min-LOD passed modes 0–3. That is only a CPU
reference check; view-minimum modes and these focused hardware probes remain TODO.
Use the workspace-root tools copy, or pass --workspace-root when using this mirror.
Any future hardware execution requires gpu-experiment.py and the normal guards.

## Rejected optimization experiments

GCM plus LICM (100.8 ms), broader loop hoisting (94.23 ms), halfword descriptor
prefetch (94.59 ms) and early GCM (96.92 ms) did not improve the approximately
92 ms baseline and were reverted. Their evidence is retained locally under
build/buoy-further and _provenance/experiments. No rejected compiler changes are
enabled by this commit.
