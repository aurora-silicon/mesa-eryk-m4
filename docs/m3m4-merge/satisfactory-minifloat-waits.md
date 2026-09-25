# Satisfactory fragment minifloat dependencies

The M4 DX11 saved-world launch rejected two fragment pipeline creations. The
retained compiler input `4f1f3e2f88b788921c0892c420e56a8251b5784a76a55a995529b3443b7d4cab`
reproduces a backend refusal of `fadd r56, #0.5` on parent `4d90cc3320a`.
Compact minifloat ALU forms can encode one scoreboard slot, or slots 1+2, but
the scheduled instruction needed a different combined dependency mask.

Commit `0255878b180` emits the existing register-preserving wait NOP before the
ALU for combinations outside that domain. It preserves every dependency and
leaves supported compact forms unchanged. The backend regression builds three
asynchronous producers and checks both the source read and destination overwrite
hazards for add and multiply. All 296 compiler tests pass. The original shader
now compiles to 220770 bytes, binary hash `63d355fb`.

Hardware evidence `20260925T040143710912Z-satisfactory-dx11-full-gameplay-stream`
ran the real saved world on M4 with the fixed build, 3840x2160 output, and the
original graphics settings. The HUD, landscape and moving creatures were visible;
DXVK logged no fragment compiler rejection. The guard completed normally after
recording 878 complete Vulkan frames (the bounded stop leaves an incomplete tail).
Peak monitored temperature was 110.5 C under the authorized 120 C guard.

This establishes compiler acceptance and a live rendered scene, not independent
pixel correctness or complete Satisfactory renderer support. The game data and
compiler input are retained only in the local benchmark repository. M3 was not
tested as requested. DX12, Vulkan, and the 30-frame gameplay extraction remain
separate validation tasks.
