# Broader read-only preamble replay qualification

Candidate a3de7e6623f is a complete matched Vulkan/Zink/EGL package on both
GPUs. HK_M4_NATIVE_PREAMBLE=true and AGX_NATIVE_MEMORY_PREAMBLE=true remain
experimental. No image tolerance, frame range or game setting changed.

M3 SuperTuxKart through Zink had one failed check: 685 pixels differ at the
middle sampled frame, maximum RGB delta 185; first and last frames match.
Control with preambles off and arithmetic-only preambles each match exactly.
However, a fresh-cache capture, three warm repetitions and two repetitions
using the preserved old cache also match with both options enabled. Replaying
0 A.D. Zink followed by native Vulkan SuperTuxKart and Zink SuperTuxKart passes.
Ten further cold-cache checks alternate off/on and all match (five each).
Each of the 32 retained compiler inputs produces identical output in ten host
repetitions. These checks do not establish a root cause or erase the failure.
Do not enable read-only preambles by default on the strength of passing retries.

Private source/capture data and full image/log records are committed locally in
gpu-replay-bench/provenance/roadmap-20260928-preamble-games. Original failed run:
20260928T112336497927Z-m3-supertuxkart-zink-check. Broader M3 checks continue with
this unresolved issue explicitly retained. M4 has passed through Frostpunk and
Satisfactory DX11 menu; Satisfactory gameplay is ongoing.

The first M4 0 A.D. attempt used the superseded reference map. It reproduced the
previously documented buffer-address fallback reference mismatch. A comparison
to the existing approved fallback images is exact, as is the corrected full
replay; both attempts remain recorded. This is a harness reference-selection
correction, not a new tolerance or a replacement reference.

Current three-sample 4K native 0 A.D. medians: M3 58.61 FPS, M4 38.78 FPS,
consistent with the earlier matched controls in timing-counters.md. Early M4
browser timing overlapped evidence archival and must not support an isolated
performance claim. These are retained replays, not new desktop qualifications.

Disk preflight stopped before the 4.04-GiB Frostpunk gameplay staging. Archived
21 unused diagnostic Mesa prefixes from M4 only after checking process mappings
and desktop/service configuration, then checksum-verifying their copied files.
Complete restorable trees live under build/driver-roadmap-20260928/
offloaded-m4-diagnostic-mesa. Target trace cleanup preserves the local corpus.
