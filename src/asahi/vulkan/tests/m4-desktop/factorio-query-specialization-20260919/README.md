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
