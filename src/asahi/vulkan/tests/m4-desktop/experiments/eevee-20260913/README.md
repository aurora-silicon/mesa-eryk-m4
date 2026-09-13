# Eevee checkpoint, 2026-09-13

Image-write completion fix 4f2c84c1585 is qualified by guarded M4 probes and
the original Eevee replay. It removes ghost geometry and striping. Cast
shadows remain missing; this is not full Eevee support or conformance.

The saved native capture is app-6918.gfxr, SHA256
4c4def3d21bc310b7c02836576ef08d57a725fffb771afdbf419566b108e6391.
Final readback: buffer_2779556529552208803.bin, 192x144 RGBA16F.
Replay 20260913T091220853914Z has RGB means .24734/.24476/.28896,
software .23901/.23513/.27806; all-channel MAE .007248.
Depth-pyramid dispatch14812 no longer loses pixels. Mips0..5 differ at most
2.38e-6; mip6 still differs up to5.58e-5.

Lighting draw15067 reads shadow atlas1855 (2048x2048x32 R32_UINT). Layer0
is empty in both renderers. All-layer comparison 20260913T091641481345Z-eevee-shadow-all-layers confirms
only layers30/31 contain allocated pages. Reference has169648/963328
shadow-depth writes; M4 has0/43025. Clear value is0x7f7fffff, not0xffffffff.
Page allocation differs, so compare virtual page mappings before positions.

`layered-geometry-probe viewport` reproduces broken viewport selection:
lavapipe exact256pixels; M4 baseline20260913T091829768362Z bad832words.
HAL200 bind emission currently hardcodes one viewport transform.
`UNQUALIFIED-multiple-viewports.patch` is a FAILED experiment, not an
implementation to install: uploading full arrays/counts/scissors caused a
stall in guarded20260913T092119044246Z. Watchdog recovery required. The
patch is removed from active sources. Investigate native bind-tail lengths,
viewport payload/selector count, per-viewport scissor indexing and geometry
draw state before retrying. Preserve all watchdog and thermal requirements.

Native offline viewport shader source/archive is retained on the host in
build/eevee-native-viewport-vertex (G16G target, remote compiler only).
No native GPU success is claimed for that offline compilation.

Host evidence lives under _provenance/experiments; ongoing findings and
binary hashes are in _provenance/five-apps-20260912/STATUS.md.
