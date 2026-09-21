# OpenGL 3.3 on Zink


`gl-cts-probe.py GUARD_OUTPUT` runs on the target under `gpu-experiment.py`.
It records the installed desktop renderer environment, hashes the ARM64 GL CTS
binaries, runs `KHR-GL33.info.*`, and saves `cts-runner --type=gl33 --summary`.
The summary command normally exits 1 when there are no saved conformance
results; that is a plan-generation result, not six new driver failures.

The ARM64 `glcts` and `cts-runner` targets are built from the existing pinned
`reference/vk-gl-cts-1.4.6.2` checkout (`f6a29701220f34dd1407513bfe80d74ca7b392ce`).
Stage their `gl_cts`, `gles2`, `gles3`, and `gles31` data directories alongside
the binaries in `/root/gl-cts-1.4.6.2/`. The main GL33 must-pass list has 9,886
cases. Freeze its original order and hash before execution; this campaign uses
`build/gl33-zink-20260921/{cases.txt,catalog.json}`.

`gl33-cts-run.py --cases CASES --output RUN --name NAME --environment JSON`
uses the existing `cts-bulk.py` watchdog/thermal/memory guard and a private
1000 Hz headless Wayland compositor. The JSON is the probe's
`renderer-environment.json`: retain the production Zink/Honeykrisp settings,
including the desktop's existing GL 3.3 version limit. Renderer checks must
identify Apple M4, and the info QPA must identify Zink. This is headless EGL
Wayland coverage, not GLX/window-system certification.

The default is seed 1, 64x64 window surfaces. `--config 113` selects seed 2,
113x47. `--config fbo-width` and `--config fbo-height` select the seed-3
rgba8888d24s8 FBO configurations with one unspecified dimension. The plan also
contains the one-case GL30 no-default-context list. Keep results for each
configuration separate. A single configuration is not a full conformance
submission; a passing process guard is not proof that every CTS case passed.

`cts-bulk.py` and `cts-sample.py` accept repeatable
`--deqp-arg=--option=value` for these GL surface/context settings, forwarding
them unchanged and recording them in the runner manifest. Existing Vulkan
commands with no such arguments are unchanged. No skip list, failure baseline,
or automatic retry masks the first attempt.

`gl-cts-report.py --output REPORT RUN...` writes every outcome, separate
failure JSON and case list, and non-pass TSV. NotSupported and warnings stay
visible and are never converted to passes. Original QPAs remain in each run's
guarded evidence directory.
