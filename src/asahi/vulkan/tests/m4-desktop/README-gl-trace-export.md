# Export the actual Hyprland GL stream

`gl-trace-reproducer.py TRACE --last-call NUMBER --output NEW_DIRECTORY` exports
a trusted local apitrace capture to `replay.c`, content-addressed buffer/shader
payloads, and a manifest. It retains GL mutations through that call and rejects
unsupported import shapes. `--apitrace PATH` selects the executable and its
adjacent `lib/apitrace/scripts` pickle modules; the workspace default is under
`build/apitrace-host/usr`. The tracked source is also in Mesa's
`src/asahi/vulkan/tests/m4-desktop/`.

Compile `replay.c` with EGL and desktop GL (`cc replay.c -lEGL -lGL -o replay`).
Run from the export directory so payload paths resolve. A host reference uses
`M4_TEST_LLVMPIPE=1 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe ./replay`.
Cross-compile the same source to `m4-replay` and stage the complete directory on
J713. Stage `run-gl-trace-export.py` and `run-m4-gl.py` in `/root`; invoke through
`gpu-experiment.py`, for example:

```
python3 tools/gpu-experiment.py --name captured-frame \
  --hypothesis 'Replay every captured draw with normal batching' \
  --expect 'PASS: captured GL draws finished' --seconds 35 -- \
  env ZINK_DEBUG= AGX_MESA_DEBUG= HK_G17P_LAUNCH_TRACE=1 \
  python3 /root/run-gl-trace-export.py /root/hypr-gl-replay '{output}'
```

The target runner stops display clients with a bounded quiescence interval,
records source/executable identities and diagnostic controls, and saves readback.
It requires the outer watchdog/thermal guard; it never renews the watchdog.
An application completion message is insufficient: Zink has returned from
readback after device loss, so the outer kernel/guard verdict is authoritative.

This is a rendering diagnostic, not a WSI replay: native XR24/AR24 renderbuffer
imports become retained local RGBA8 storage; texture imports use apitrace's
captured TexImage2D payload. EGL fences, presentation and context transitions
are omitted. The manifest lists adaptations. Reject unsupported captures rather
than replacing missing texture data. Do not use untrusted pickle input.

Optional `M4_GL_REPLAY_FINISH_BEFORE=CALL`, `M4_GL_REPLAY_SKIP_DRAWS=CALL,...`
and `M4_GL_REPLAY_REPEAT_DRAW=CALL` are explicit causal controls, never production
fixes. With all three unset, the original 177-draw prefix through call 5170
reproduced the Hyprland border hang. The compiler's coverage-ordering fix then
passed that unchanged prefix and the normal SDDM login. The readback matched
llvmpipe within one RGB level with exact alpha across all 4,259,840 pixels.
