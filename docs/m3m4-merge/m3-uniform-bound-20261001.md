# M3 uniform-loader sizing cleanup, 2026-10-01

The completed-interval CPU diagnostic samples uniform-loader sizing as well as
range construction. Sizing previously called agx_ioto_load_target_encoding two
or three times for the same chip. It now resolves the immutable encoding once.
Every alignment, address-end overflow, range, chunk and size-overflow check is
retained, including transactional outputs on rejection and empty GPU0/GPU1
behavior. The emitted resource programs and shaders do not change. ARM64
object disassembly confirms one call in the bound routine.

The existing host program/template suite passes all 28,406 checks, covering
GPU0/GPU1 chip cohorts, exact instruction bytes and invalid/overlapping
workspaces. No new implementation-mirroring unit test is added. On M3, the
48 multi-draw resource pixel oracles and six original game image checks pass:
Minecraft OpenGL/Zink and Vulkan, Satisfactory Vulkan gameplay, Factorio
Wayland, 0 A.D. Vulkan and SuperTuxKart/Zink. References, tolerances, API calls
and scene settings are unchanged. This is targeted qualification, not full CTS
or new M4 hardware qualification.

Nine alternating pairs use the same boot, kernel, capture, replayer and
Gallium library. Control 312c3e934868 has median 77.0102 FPS; candidate
5b2b97a00a95 has median 80.1647 FPS. The median *adjacent paired* change is
only +0.2680%, with five of nine positive pairs. This is within the observed
variation and is not called an established replay FPS gain. Keep this as a
small CPU cleanup, not a reported three-FPS speedup. The main results.html
and normal f568 desktop prefix remain unchanged. The earlier separate
recording-policy cache result (+1.4009% paired) stays separately attributed.

All 22 paired segments, the resource guard and six game guards complete with
60-second watchdogs, all-channel 0.5-second PMP monitoring and unchanged
thermal policy. No GPU fault or reboot occurs; boot is bd963178-2bf1-490d-84d2-
e698c51d8b68, kernel module 989b373fb859. Pair-run peak PMP is 91.75 C.

The first resource invocation used a nonexistent source path and stopped at
scp before any hardware experiment; retain the receipt rather than counting
it as a GPU test. The reusable resource runner now validates its source before
creating either local or remote setup. Its source is mirrored under tools/ in
this document's directory. A missing-file invocation proves no output directory
is created; the valid source was used for the actual guarded oracle.

Candidate prefix: /opt/asahi-m3-uniform-bound-hoist-5b2b97a00a95.
Vulkan SHA256: 5b2b97a00a957e93744ad462a068548e3f986a59f553fd6c1d96ac6f7aa75a62.
Workspace receipts: build/m3-uniform-bound-20261001/{paired,qualification.json}.
Small comparisons/source patches/guard receipts are committed locally under
gpu-replay-bench/diagnostics/m3-uniform-bound-20261001/; complete captures,
images and replay runs remain outside Git.

The current kernel wait path already uses IRQ notifications, four bounded
5-us polls after a notification and a 100-us high-resolution timer. It does
not use an unconditional millisecond sleep for each GPU retirement. The
retained global render/compute span gaps include firmware and retirement work
and overlapping clients; they cannot establish that every gap is CPU scheduling.
No wait policy, full-idle retirement check, barrier or kernel default is changed
in this cleanup.
