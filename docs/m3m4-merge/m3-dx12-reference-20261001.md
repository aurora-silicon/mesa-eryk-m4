# M3 Satisfactory DX12 reference isolation, 2026-10-01

The current driver's large difference from the September26 reference is caused
by the September28 front-facing helper-lifetime correction, not the October1
kernel timeline repair. This reconfirms the earlier private control in
`roadmap-20260928/frostpunk-preamble-investigation.md`; it is not a new shader
bug fix. Removing only BACKFACING from the helper-retention rule in
`agx3_pack_binary.c` recovers nearly all of the old output. Maximum errors drop
from115/91/103 to2/2/3, and changed pixels from13–18k to49/231/803 at frames
6/20/35. The remaining result still fails the unchanged32-pixel budget.

Reverting the marker is not an acceptable repair. The independently specified
mixed-facing derivative oracle in `src/asahi/compiler/test/probes/` passes all
3969 exact-green pixels on current M3 Mesa and host lavapipe. The private
old-behaviour control fails at pixel7. Shader headers and the reused M3 probe's
SHA are pinned; the host fixture changes only device selection. This agrees
with the original2058-case M3/M4 front-facing/helper qualification and the
Frostpunk software normal calculation. A same-driver old image is not an
independent API oracle and can preserve a pre-existing bug.

The private library is a476a19e52f9, with its exact two-line source patch retained
in the local benchmark diagnostics. Current oracle library312c3e934868 keeps
the qualified marker. Both short M3 guards record60-second watchdogs, healthy
completion/disarm and all-channel temperatures (peaks53.25/57C). The first
full-scene control hits its37-second inner process bound before saving images;
its guard proves a reaped workload and healthy idle GPU before disarming. A
warm retry under the existing70-second retirement-progress guard completes
in22seconds. Neither attempt reboots or changes normal Plasma's f568 prefix.
Source and host-generated/ARM64 libraries are restored after the private build.

No reference replacement, tolerance expansion or new DX12 timing approval is
made. An independent complete-scene reference or shader-specific output proof
is still needed before approving corrected DX12 output; the sparse residual
also remains unresolved. No M1 or M4 hardware was accessed for this follow-up.
Complete images/captures stay outside Git; small manifests, comparisons, guard
receipts, source patch and expected oracle failure remain committed locally in
`gpu-replay-bench/diagnostics/m3-dx12-reference-20261001/`. Workspace evidence is
`build/m3-dx12-reference-20261001/`.
