# Shader-cache configuration regression

The pipeline/shader-binary UUID and disk-cache namespace included the build, GPU contract and compiler debug bits, but omitted driconf and HK_PERFTEST. Changing hk_disable_border_emulation could therefore reuse a shader compiled with the opposite setting. Shader outputs from such a configuration-only experiment are inconclusive.

Hash the parsed driconf option set and HK_PERFTEST string into the cache contract. Use that full digest for the on-disk namespace as well as the pipeline and shader-binary UUIDs. Extra invalidations for non-codegen options or equivalent flag spellings are conservative and intentional.

The companion cache-options-probe.c only queries physical-device properties. run-cache-options-probe.py runs separate processes for border disabled/enabled, HK_PERFTEST=noborder, and a repeated first configuration, under the existing watchdog/thermal wrapper. It asserts distinct pipeline and shader UUIDs and repeat stability; --expect-collision reproduces the previous bug. Build the C file against Vulkan with the target toolchain.

Before: all three settings yield identical UUIDs on M3 (driver 764f636a1b401eed28580f1815eab5ac8f0e0f6d0bf15cb3c45f57233dfc7342). Fixed: three distinct namespaces, stable on repetition, on both M3 and M4 (driver 17b613cabb1a7948e2a2e7b9522ab2a4a395a050070b97f7b029e0b16bc70f53). Exact observations and executable hashes are retained alongside this file. Both host and ARM64 driver builds succeed.

Rendering regression: the retained indirect Satisfactory shadow draw completes on both GPUs; their depth readbacks are byte-identical, have 72 covered pixels, and match lavapipe coverage with maximum depth error 2.4281689547933638e-08. Game assets remain in the local private benchmark repository. The M4 outer report expected the wrong text marker ('nonzero') and reports missing_success_evidence; its guarded child exited zero with DRAW_COMPLETED, and the independent depth comparison supplies the actual validation. No raw guard record was rewritten.

This change fixes cache identity. It does not claim to resolve the outstanding temporal DX12 image differences.
