# Full OpenGL ES 3.2 test campaign (2026-09-22)

The actual `cts-runner --type=es32 --summary` plan contains 326,602
configuration/case pairs across 87 sessions. It includes EGL, AOSP ES2/ES3/ES31,
KHR ES2/ES3/ES31/ES32, single-config shader cases, no-context tests, and ES
extension tests. Unlike the prior desktop GL main-list runs, every session
and prescribed EGL configuration in this plan is selected. The separate
CTS-Configs inventory is recorded in addition to these test pairs.

Source/binary baseline is upstream CTS f6a29701220f34dd1407513bfe80d74ca7b392ce
(v1.4.6.2). No sparse-clamp diagnostic patch is used for this ES campaign.
Renderer discovery reports native ES 3.2 and Zink/Honeykrisp Apple M4.
Evidence: 20260922T060322693223Z-gles32-headless-plan.

`gl-cts-plan.py freeze` retains exact ordered lists, hashes, per-configuration
arguments, global configuration/case indices, and 2,500-case segments. `run`
uses the existing watchdog/thermal/memory guard, with 60-second per-case bounds
and a 900-second segment bound. It stops on fatal or incomplete execution;
ordinary failed comparisons are retained and execution continues. No automatic
retry or failure baseline suppresses errors. `report` preserves each result,
its original global index, and the owning session (the same case under another
configuration is a separate test obligation).

EGL config IDs are enumerated under a private root Xwayland. Execution uses
that same X11 EGL backend on a private 1000 Hz headless Hyprland; window rules
allow exact CTS resizing. Every segment has a fresh compositor. The panel's
normal desktop remains running. ES version overrides are removed, not raised.
EGL tests run serially; GL/ES shader groups use four workers after qualification.
This does not change Vulkan WSI's one-worker-per-compositor requirement.

The runner cannot perform its renderer check on dEQP-EGL. Those segments first
run KHR-GLES32.info.renderer with the same binary/environment inside the guard,
verify its QPA against Apple M4, then execute the EGL list. Other groups retain
the runner's normal renderer check. There is no unchecked EGL execution.

Two harness setup errors before the first EGL batch (missing provenance field,
and unsupported deqp-runner EGL renderer-check option) started no CTS tests.
Their directories remain under segment 0000 as `run-setup-*`, with full evidence.
The result collector now records Unrun when setup never created runner.json.

Working evidence: `build/gles32-zink-20260922/full`. These are development
results, not a Khronos certification claim. Running state and final results
will be recorded here once the selected sessions complete.
