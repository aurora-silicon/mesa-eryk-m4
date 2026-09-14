# Vulkan 1.4 sample, seed 20260930

Frozen broad Linux sample: 20,000 cases, 20 workers, 50 cases per group,
60-second per-case bound. SHA256:
`8402669bf8a2cbef4a5635106f622b0f18a5bfddc1e571665de81e20f8e3881c`.
The separately identified CTS binary includes the documented seed29 reference
corrections. Original cases and outcomes remain preserved.

This campaign is in progress. Run1 completed 8,123 cases before a timeout;
run2 completed 722 more before a userspace compiler assertion. The descriptor
case's 2.417-second isolated pass does **not** resolve its 60-second contended
timeout. It remains an issue to fix and validate under contention.

The i16 absolute-value crash now passes: sign-extend narrow inputs, perform
word arithmetic and truncate the result, retaining two's-complement INT_MIN.
The vertex array texel-fetch case passes with the native register-LOD path
allowed in vertex stages. The large transform-feedback output case passes
with spill reloads between ordered exports; the existing nonterminal staging
retirement preserves sources before subsequent loads reuse their registers.

A reverse optimizer fusion also produced NOR although the M4 encoder accepts
separate OR and NOT. Apply the same supported-truth-table gate used for forward
fusion. The host optimizer check passes; the original boolean-subgroup case
now reaches a separate quad-ballot encoding boundary and is still failing.

Targeted evidence: compiler-fix1, three Pass and one Fail, 0.810 seconds,
peak 60.25 C, mandatory thermal guard and 60-second watchdog. Installed Mesa:
`38218b65ac9e5adbb624510f8e790c33f80fd4c6a545453f23d650dd88e5e85f`.
No broad regression or conformance claim.
