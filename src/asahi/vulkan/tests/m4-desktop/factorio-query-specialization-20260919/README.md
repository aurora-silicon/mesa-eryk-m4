# Specialize native query result copies

HK_M4_SPECIALIZE_QUERY_COPY=true specializes the internal M4 query-copy
pipeline by Vulkan result flags and availability layout (timestamp sentinel,
u32 status, or u64 status). A structured meta pipeline key covers every choice.
Pointer values remain dynamic; no application shaders or names are recognized.
The compiler removes unreachable result-width and status branches. WAIT keeps
a runtime nonzero-destination guard, preserving the grounded native loop nesting.
Default false pending performance qualification and broader CTS.

Native ARM64 Mesa build passes. Guarded J713 evidence:
- 20260919T052409817138Z: 12 host-reset timestamp cases, flags 0..7 with all
  queries written, flags 0/1/4/5 with odd queries unavailable. CPU/GPU results,
  32/64-bit words, availability and untouched padding/destination bytes pass.
  Peak 64.75 C. A 64-bit NO-WAIT availability copy is 1744 bytes, versus the
  previous generic 8064-byte shader. This is not an FPS claim.
- 20260919T052428986324Z: empty fragment-statistics WAIT copy with native
  u64 completion availability passes.
- 20260919T052451282342Z: ordinary u32 statistics availability passes.

Probe sources and runner are mirrored here; their reusable originals live in
workspace tools/. Run only under gpu-experiment.py with its 60-second watchdog,
six-channel temperature monitor and 95 C cutoff. Modes 16/17 deliberately use
host reset; the separate GPU-reset visibility failure remains unresolved.
This change does not enable the rejected HK_M4_BATCH_META_FILLS option.

The retained frame 20260919T052503467708Z is pixel-identical (zero differing
pixels, identical PNG SHA256 88594c10407e243eb0617860b8f2abc1f06c0844ec67ddba3900875d7e36796e).
The unchanged 60-second live menu 20260919T052541131310Z completes at
40.925 FPS (40.24--41.48 tail), peak 89.75 C.
Different animated scenes prevent a paired speedup claim. Sustained 60 FPS
remains unmet; query specialization stays optional pending further work.

Combined with branch preservation, integer-only preambles and descriptor
prefetch, the original replay also matches exactly (052724946075Z). The live
menu 052829801140Z averages 36.495 FPS, range 17.10--45.07, peak 86.75 C.
This is not a qualified performance improvement; the options stay opt-in.
The desktop separately has descriptor prefetch enabled after a guarded
autologin restart (052032630230Z), with unchanged display settings.

Temporary vblank_mode=0 diagnostic 053424631067Z completed at 40.106 FPS,
35.98--41.59 tail, peak 91 C. It did not remove the bottleneck. The outer
runner says missing_success_evidence because its requested string was PASS
but the observer prints FACTORIO_STANDALONE_INITIALISED; process exited zero
and all 60 seconds were retained. Keep that distinction; do not rewrite the
verdict. Normal vblank behavior remains the acceptance configuration.
