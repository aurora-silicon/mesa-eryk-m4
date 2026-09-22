# M3 GLES array gradient offsets (2026-09-22)

All ten GLES3 `texturegradoffset` 2D-array cases failed through Zink on the
M3: fixed/float, signed/unsigned integer and shadow sampling in vertex and
fragment stages. The current pre-fix build reproduced all ten failures.

Honeykrisp selected NIR coordinate-offset lowering for array gradients
without a minimum LOD. It added offset / base-level-size before sampling;
gradients may select a smaller mip, where the texel offset has a different
normalized size. Coordinate adjustment also cannot represent different
offsets for the two levels of a trilinear sample.

Preserve these offsets for the existing native texture-sampling encoder,
as already done for the view-minimum variant. Keep fetch and unsupported
gather lowering. No CTS expectations, shader references or advertised
features were changed.

## Hardware regression

Pinned CTS v1.4.6.2, the unchanged ES31 plan's session 2 options, private
headless Weston/Xwayland, one worker. Each segment used the 45-second M3
guard, 60-second watchdog and five-channel 0.5-second temperature monitor
with a 95 C stop. Boot `1c5cdaba-93d8-4344-b10d-8a5e7c75ba9b`.

All ten exact reproducers pass, followed by all 586 shader texture-function
cases in that session. No process crash, GPU fault or reboot was observed.
These focused results do not establish full GLES conformance.

- `build/m3-gles31-20260922/triage-array-grad-offset/native`: 10 Pass, peak 48.75 C; results SHA256 `e6e618d84139a812908177b34c1df548cbf3b956d69f3b60075f0b0e96069158`.
- `build/m3-gles31-20260922/triage-texture-functions/00/run`: 100 Pass, peak 47.25 C; results SHA256 `c7dd1eaafe90f78beed09e3728e9c0e8e08822cca16a0cbcc89af29d2d3b5339`.
- `build/m3-gles31-20260922/triage-texture-functions/01/run`: 100 Pass, peak 47.25 C; results SHA256 `ea2c7e7d2ecd947795e8d48289afc9afea0a592c5f15a7d0fe634177ca699d3c`.
- `build/m3-gles31-20260922/triage-texture-functions/02/run`: 100 Pass, peak 48.00 C; results SHA256 `5633b290901aa9fc89e8ebf62ee058276395193082d2b00bb23939729142e124`.
- `build/m3-gles31-20260922/triage-texture-functions/03/run`: 100 Pass, peak 47.25 C; results SHA256 `6fe3b48234ecd9b5dfc44275752cd4ca2cc819e43aead6e204919eb3bb2d47da`.
- `build/m3-gles31-20260922/triage-texture-functions/04/run`: 100 Pass, peak 47.00 C; results SHA256 `c6ee4b7a002c4ececf66ea28f7c32e9b603992dd5cd81195de939e6695a822d6`.
- `build/m3-gles31-20260922/triage-texture-functions/05/run`: 86 Pass, peak 50.00 C; results SHA256 `56cd9c948ef682adbb9e182f654d0e63d1578d1d1db88496b0656fa9ecdb0f49`.
