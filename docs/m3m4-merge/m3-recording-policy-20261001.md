# M3 immutable recording policy, 2026-10-01

A completed-interval CPU profile finds repeated environment lookups in native
resource-template and fragment-statistics state construction. The driver now
reads those choices at device creation and keeps them immutable for that device.
It does not cache buffer values or change shader arithmetic, resources, calls,
scene settings or GPU clocks. HK_CACHE_RECORDING_OPTIONS=false restores the
per-draw diagnostic lookups. The default cache policy is enabled on G15S only;
other chips retain their previous path unless explicitly opted in. The existing
template and statistics overrides still select the same values at creation.

The first separate-binary candidate b1579796692b loses 5.21% in nine adjacent
pairs against installed f568; it is not promoted or called a speedup. A second
experiment keeps both paths in one binary (8f4befa3fc84), alternates pair order
and retains every sample. All nine pairs improve; the median adjacent change
is +1.4009%. Separate medians are 76.4277 and 81.0258 FPS, but that larger gap
is not attributed to the change. These short intervals occupy multiple timing
groups. This is a small measured recording-path improvement, not a large GPU
throughput gain or a new live Java FPS claim. Both sets retain exact images.

The final G15S-default binary 312c3e934868 passes all 308 frozen targeted CTS
cases: 263 Pass, 45 NotSupported, zero failures, all seven guards complete.
Catalog indices/hash are retained. The 48 resource pixel oracles pass with
caching disabled and enabled. Six original game image checks also pass:
Minecraft OpenGL/Zink and native Vulkan, Satisfactory Vulkan gameplay, 0 A.D.
Vulkan, Factorio Linux Wayland and SuperTuxKart/Zink. Existing references and
tolerances are unchanged. This is targeted M3 qualification, not full CTS or
M4 hardware qualification. The normal desktop remains on f568.

Implementation: 1d3cb765de9. Final Vulkan SHA256:
312c3e934868497f8a8ffbd4c3e8e9a7c269890dd27c085c05a0ec07a2bf81d4.
Final prefix: /opt/asahi-m3-recording-policy-m3-312c3e934868.
Boot: bd963178-2bf1-490d-84d2-e698c51d8b68; kernel module 989b373fb859.
The 60-second watchdog, all-channel 0.5-second PMP monitoring and original
thermal policy cover every hardware segment. No GPU fault or reboot occurs.
Complete replay evidence stays outside Git in the local benchmark tree; small
receipts and candidate source patches are retained in diagnostics.

The reusable tools/m3-replay-paired.py wrapper, mirrored in this directory's
tools/, requires an approved reference before starting hardware work. It uses
independently guarded check/warmup/timing segments, exact RGB approval, explicit
per-configuration correctness checks for timing import, one boot and immutable
identities, and alternating pair order. It reports the median paired change
separately from the two FPS medians, preserving negative/low samples. It neither
changes desktop selection nor relaxes tolerances; use benchmark-suite.py for
cases with an already documented non-exact image policy.

Workspace evidence: build/m3-recording-policy-20261001/qualification.json.
Private benchmark diagnostics: m3-recording-policy-20261001-r2 (first loss),
m3-recording-policy-toggle-20261001 (same-binary pairs), and
m3-recording-policy-final-20261001 (final qualification). The original first
runner invocation had an invalid reference path and stopped at host approval
after a healthy guarded control; its receipt is kept separately and the helper
now checks the reference before arming hardware. No failed setup is counted.
