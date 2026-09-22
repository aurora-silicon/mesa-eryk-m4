# M3 vertex texture gathers (2026-09-23)

`KHR-GLES31.core.texture_gather.plain-gather-depth-2darray` crashed during
shader compilation in the frozen ES 3.1 campaign. A fresh singleton reproduced
`texture_sample` rejection followed by Zink's failed pipeline creation and
SIGSEGV. The GPU and guard stayed healthy; the three SIGKILL peers in the
original batch were canceled when the runner stopped.

Two stage restrictions rejected an otherwise supported native operation:
indexed sampler lowering accepted only explicit levels/gradients in vertex
stages, and instruction packing allowed gathers only in compute/fragment.
Base-level gathers need no fragment derivatives. Admit them in both paths,
including the software geometry/tessellation stages that use those resource
operations. Ordinary implicit-LOD samples remain rejected outside fragment.
No CTS expectations, capabilities or shader calculations change.

The gate-only intermediate build still rejected the unlowered descriptor
operands; its crash evidence is retained. With both changes the exact unchanged
case passes image verification on M3. Backend tests cover ordinary and shadow
array gathers across five non-fragment stages, plus rejection of implicit-LOD
samples in those stages. Indexed sampler and array-gather compile checks pass.

Evidence root: `build/m3-gles31-20260922/`:

- `campaign/segments/0491/run`: original batch crash and canceled peers.
- `triage-gather-array-{baseline,debug}`: isolated reproduction and shader dump.
- `triage-vertex-gather-candidate`: incomplete gate-only change, still Crash.
- `triage-vertex-gather-lowered`: both changes, exact case Pass.
- `triage-vertex-gather-regression`: complete gather group across configurations.
- `triage-vertex-gather-peers`: independent follow-ups for canceled cases.

Hardware evidence for this change is on M3 only. Offline acceptance of other
shader stages is not a hardware qualification of unadvertised features.

The full focused group completed **525 Pass** (35 cases × 15 framebuffer
configurations) under the unchanged 45s guards, peaking at **53°C** across the
five PMP channels. Every guard and batch completed; raw results and hashes are
listed in its validation manifest. This is focused coverage, not a whole-suite
conformance claim.

All six canceled peers pass in fresh singleton runs. The three raw SIGKILL
Crash records link to these validations as coordinated-stop follow-ups, not
independent compiler failures; the three Interrupted records retain their
statuses and separate diagnostics. The repaired build is installed for new
production processes. Its Vulkan library SHA256 is
`fbb021b0682175f5e1647443f1315c13499f821bcf3e08984fc9fd6fec1d4931`.
