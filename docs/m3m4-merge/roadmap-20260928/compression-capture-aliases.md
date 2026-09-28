# Compression and captured memory aliases (2026-09-28)

This investigation does not enable compression or approve new performance
scores. The complete dd452c4dd66 package and original Satisfactory DX11 gameplay
capture are retained. Both GPUs initially produce large errors when compression
is enabled with the capture's original memory layout. Rebinding removes most
errors, but both GPUs retain exactly the same remaining differences.

Width-selected diagnostics isolate those differences to three 128x128 BGRA8
textures (capture IDs 266, 33609 and 33622). Their allocations overlap raw buffers
covering entire captured heaps. Render-target-only compression with rebinding
is exact on M3. Compressing only the 128x8192 texture is also exact. Small and
tall captured texture bytes pass independent full/partial Vulkan copy probes;
M3 RGBA8 UNORM/SRGB texelFetch and linear sampling controls agree bit-exactly
with compression disabled, including float render-target readback. These
probes do not qualify every format, aliasing usage or sampling mode.

A diagnostic GFXReconstruct build allocates each replay image separately,
removing its captured memory aliases. With full compression and rebinding, M3
then matches all three ordinary reference frames byte-for-byte. This strongly
implicates the recorded buffer/image memory layout, rather than demonstrating
that hardware compression itself is broken. It does not establish that all
aliases are dispensable: changing this relationship is an explicit diagnostic
adaptation, not valid benchmark qualification or an application-support claim.
The ordinary replayer and Mesa sources were restored and rebuilt; no allocation
workaround or image-specific compiler rule was committed.

Two cold M3 nopromote controls stopped at the unchanged GPU-retirement progress
gate and recovered by watchdog. No firmware fault snapshot was recorded, health
remained true, and temperatures were below the thermal stop. The monitored
retry shows CPU pipeline workers accumulating execution time while the main
thread waits; a further compute retirement also occurs before the guard ends.
This is evidence of an initialization/progress-policy limitation, not proof of
a hardware hang or a fixed failure. The current v1 completion ABI has no idle
state. CPU activity alone must not authorize watchdog renewal.

Private evidence is in gpu-replay-bench/provenance/roadmap-20260928-compression-aliases:
comparisons.json, exact texture bytes, original failed and successful runs,
diagnostic patches/binaries, independent probes and CPU stack samples. The
alias-detached result remains diagnostic_only, with no approved timing score.
