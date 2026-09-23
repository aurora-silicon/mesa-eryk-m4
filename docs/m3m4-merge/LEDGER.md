# Integration ledger — 2026-09-23

This is the explicit post-cutoff update to `M3_M4_MESA_MERGE_PLAN.md`.
Source dispositions below are integration intentions; build and execution gates remain pending until separately recorded. No prior result is a result for this candidate.

## Frozen endpoints

M3 `a9a2aa7e00df758a1edc0836c9638af558b8b38a`; M4 `08b19ec3a2e422a7252fdfe77c185450fccf86da`; common ancestor `6a30d044f344be079a296357fbc97fdaa7765c5d`.

M4 has no post-cutoff production change. Its later evidence records a repaired kernel, 102,857 repeated ES pairs and incomplete sustained validation. M3 adds compiler width/gather fixes, sampler offset/border fixes, XFB lifetime, SNORM copies, clip output, fragment storage, MSAA limits, and Zink/GL correctness fixes. These replace the older plan’s prohibition on *already qualified* M3 fragment storage; vertex storage, geometry/tessellation and sparse remain disabled on M3.

The refreshed production comparison has 76 files (the original frozen map remains 74). One encoder and target-specific submission/VM/sampler contracts remain the design. No feature union or relaxed thermal policy is authorized by sharing code.

## Unique changes

| Target / origin | Purpose | Disposition / dependency and rollback unit |
| --- | --- | --- |
| M4 `4cecb0532de2` | agx: keep inserted G16G image fences free of workgroup rendezvous | preserve from M4; origin commit is the review/rollback unit |
| M4 `093ebf33bd13` | agx: serialize vertex exports outside terminal batches | preserve from M4; origin commit is the review/rollback unit |
| M4 `dba74f41b331` | agx: accept array colour gradients with register offsets | preserve from M4; origin commit is the review/rollback unit |
| M4 `726c43a3f5ec` | hk: give application memory mappings independent lifetimes and bounds | preserve from M4; origin commit is the review/rollback unit |
| M4 `abd7178cb638` | agx: merge shader-part exports at the final exit | preserve from M4; origin commit is the review/rollback unit |
| M4 `66eda73afadb` | agx: avoid quadratic searches of live and spilled values | preserve from M4; origin commit is the review/rollback unit |
| M4 `fcd945b61fd3` | agx: use the unsigned scratch offset range for AGX3 spills | preserve from M4; origin commit is the review/rollback unit |
| M4 `60e30992b5f8` | agx: preserve subtraction of full-width IMAD addends | preserve from M4; origin commit is the review/rollback unit |
| M4 `0ec29f0eae07` | agx: print malformed operands without aborting diagnostics | preserve from M4; origin commit is the review/rollback unit |
| M4 `291e1ff58d80` | vulkan: derive public pipeline binary keys from serialized content | preserve from M4; origin commit is the review/rollback unit |
| M4 `327f40eb6911` | asahi: align compiler tests with retained registers and shuffle scratch | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `bb2d6c1d3599` | asahi: test subgroup lifetime hints and non-destructive waits | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `53ca9eb7b873` | asahi: update atomic protocol tests and supported ALU domains | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `d743270db3a2` | asahi: repair stale pixel, memory and control-flow test contracts | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `9fb3ee742d8f` | asahi: cover staged textures and load retirement before branches | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `5eddcdcdfc3d` | asahi: verify image-store lifetime and load retirement ordering | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `54dc3f802f8c` | asahi: sequence pixel-core encoding before reading output length | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `65e3d6ef318b` | asahi: document the completed compiler-unit audit and retain bitrev coverage | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `012214ce783d` | hk: respect binding boundaries in descriptor writes | preserve from M4; origin commit is the review/rollback unit |
| M4 `fc263a102288` | asahi: retain qualified SuperTuxKart Zink and Vulkan benchmarks | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `8e6543d18d73` | hk: prefetch dynamic and non-robust fragment UBO reads | preserve from M4; origin commit is the review/rollback unit |
| M4 `c01261c45feb` | asahi: record live SuperTuxKart UBO optimization results | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `15bfe849f8c4` | asahi: retain matched 4K macOS SuperTuxKart comparison | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `8e0c93c3b4d4` | zink: preserve first-use image acquisition across batch lifetime tracking | preserve from M4; origin commit is the review/rollback unit |
| M4 `4d702078263a` | asahi: profile bounded Firefox playback without repeated image captures | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `95bd3fec16f3` | asahi: pin Firefox benchmark geometry across driver reboots | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `c63674aea24f` | asahi: retain Firefox window placement and allow navigation replies | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `5686c7dbf028` | asahi: reject hidden Firefox benchmarks and summarize submission costs | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `ea8f4d5f2e0b` | asahi: retain steady 1080p Firefox validation and clean closed probe profiles | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `ee03f28a91f0` | asahi: hand verified YouTube playback to an attended desktop session | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `f9f833fc2915` | asahi: run OpenGL 3.3 CTS through guarded Zink harness | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `2a2c389c11ca` | asahi: retain indexed GL CTS coverage across bounded segments | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `91f2067c2ae7` | asahi: record completed GL33 Zink main-list results | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `b195ca5c2375` | hk: allow native indexed draws with input assembly queries | preserve from M4; origin commit is the review/rollback unit |
| M4 `4900951f515b` | hk: preserve firstIndex when counting direct restart draws | preserve from M4; origin commit is the review/rollback unit |
| M4 `42698efe9ae1` | zink: preserve query render-pass scope and normalize line-loop buffer results | preserve from M4; origin commit is the review/rollback unit |
| M4 `941a4f5c06dd` | zink: advance copied query ranges once across parallel pools | preserve from M4; origin commit is the review/rollback unit |
| M4 `6db670204f23` | hk: implement GPU conditional rendering for draws, dispatches and clears | preserve from M4; origin commit is the review/rollback unit |
| M4 `abf45fe0a861` | hk: implement transform-feedback byte-count draws | preserve from M4; origin commit is the review/rollback unit |
| M4 `e5a35fceb50f` | hk: batch native buffer copies with complete destination ledgers | preserve from M4; origin commit is the review/rollback unit |
| M4 `dab4eed852d1` | agx: widen half inputs before native unary math | preserve from M4; origin commit is the review/rollback unit |
| M4 `e8779daa3ccc` | hk: combine HAL200 sampler and instruction LOD bias before quantization | preserve from M4; origin commit is the review/rollback unit |
| M4 `e7b24ad4c75d` | hk: retain opt-in transform-feedback query retirement diagnostics | preserve from M4; origin commit is the review/rollback unit |
| M4 `c367ecc63b12` | asahi: preserve in-flight GL CTS logs and add lavapipe comparison mode | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `23cd4edc42fa` | asahi: record GL33 fixes and complete main-list recheck | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `0c0845a3a98f` | asahi: prepare unforced OpenGL 4.6 CTS coverage on Zink | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `27306c42c7cb` | agx: allow native base-level gathers in vertex and compute-lowered stages | preserve from M4; origin commit is the review/rollback unit |
| M4 `ca3db5de8caa` | agx: test indexed vertex array gathers before backend selection | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `11efbd9b19ca` | asahi: retain clean first 5000-index GL46 CTS range | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `2a83ba89a36c` | asahi: retain clean GL46 CTS coverage through index 10000 | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `b3885da16008` | hk: preserve XFB metadata before replacing shader NIR | preserve from M4; origin commit is the review/rollback unit |
| M4 `7845f806b221` | hk: preserve float and SNORM representations during native copies | preserve from M4; origin commit is the review/rollback unit |
| M4 `e27e442bfcf9` | docs: retain GL46 sparse and texture-view failure investigations | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `ca927eb38964` | docs: explain GL46 failure attribution and passing rechecks | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `257d111219d4` | hk: enable slice-based sparse 3D residency on M4 | preserve from M4; origin commit is the review/rollback unit |
| M4 `5709e4dab745` | docs: record complete Vulkan sparse 3D validation for GL46 | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `32b25c018bfa` | hk: allow undefined point coordinates for non-point primitives | preserve from M4; origin commit is the review/rollback unit |
| M4 `febd920ef4de` | docs: retain the GL46 subroutine rejection and passing group | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `493e00ba0adb` | cts: index attempts once when aggregating GL46 coverage | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `22d9b26877fb` | hk: route compressed copies across 1D image boundaries | preserve from M4; origin commit is the review/rollback unit |
| M4 `133863683f9e` | agx: decouple vertex export destinations from staging capacity | preserve from M4; origin commit is the review/rollback unit |
| M4 `0f22c4bfd9c2` | hk: expose validated 128-component M4 shader interfaces | preserve from M4; origin commit is the review/rollback unit |
| M4 `d4e32ebe2b7e` | agx: keep discard-only shader parts from running sample tests | preserve from M4; origin commit is the review/rollback unit |
| M4 `f4873bee5599` | hk: report the native M4 3D sampler limit consistently | preserve from M4; origin commit is the review/rollback unit |
| M4 `62bc5f327330` | util: retire completed resolves when synchronizing threaded contexts | preserve from M4; origin commit is the review/rollback unit |
| M4 `d05df3c616f2` | hk: implement M4 depth clip control after transform feedback | preserve from M4; origin commit is the review/rollback unit |
| M4 `f0468261f0f8` | docs: record complete GL46 main-list rechecks and classify every skip | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `259c93f463e8` | zink: expose shadow sampler LOD and bias support | preserve from M4; origin commit is the review/rollback unit |
| M4 `5963ae43ac00` | hk: implement post-depth sample coverage on M4 | preserve from M4; origin commit is the review/rollback unit |
| M4 `94587c5f24a6` | agx: preserve operand register limits when shuffling live values | preserve from M4; origin commit is the review/rollback unit |
| M4 `98e52b36bd97` | zink: fix sparse image residency extraction and offset commitments | preserve from M4; origin commit is the review/rollback unit |
| M4 `2ea24ffb5945` | hk: support sparse multisample images and native residency on M4 | preserve from M4; origin commit is the review/rollback unit |
| M4 `121e2d6523ea` | docs: validate sparse-depth CTS setup correction separately | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `e4cdc4442d18` | hk: enable qualified native advanced blending on M4 | preserve from M4; origin commit is the review/rollback unit |
| M4 `67d6856dde53` | zink: use native Vulkan advanced blend operations and coherence | preserve from M4; origin commit is the review/rollback unit |
| M4 `22064cfab48c` | docs: retain final optional-feature coverage and matched Mesa deployment | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `ae209e4cabd9` | docs: close six GL46 failures with committed CTS setup fixes | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `503c738804a8` | tests: freeze full OpenGL ES 3.2 plan and add guarded configuration runner | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `d72b5b236033` | tests: retain ES configuration inventory and reviewed compiler-abort continuations | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `20d7a348f85d` | tests: checkpoint initial ES32 campaign failures without driver changes | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `10e7c7ef2f70` | tests: count only executed attempts in ES campaign reports | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `aabbb528f4fb` | tests: summarize ES results by configuration and unique failing case | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `9633d73c9771` | tests: retain first 50k ES campaign results and staging archive record | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `46cba00dea5a` | tests: reuse verified CTS runners instead of duplicating each segment | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `343db885e7f7` | tests: expose live ES runner progress without changing saved coverage | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `50f15129d0f5` | tests: checkpoint 100k ES configuration pairs with all failures retained | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `d6b4b92e634f` | tests: continue unfinished ES cases after reviewed batch deadlines | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `8ee6a9708b29` | tests: verify no-context ES renderer with guarded context preflight | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `557312be7406` | tests: preflight renderer for older Khronos ES packages | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `8c56854b05ac` | tests: retain reviewed ES LOD-bias timeout and resume canceled peers | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `cba1ed20769e` | tests: retain reviewed tessellation timeout and exact-case continuation policy | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `157fab6b75a1` | tests: record reviewed ES32 vertex-ordering timeout | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `cfaf85c640b4` | tests: preserve ES GPU hang and guarded watchdog recovery evidence | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `c7151e628129` | tests: retain compressed firmware snapshot from ES campaign GPU timeout | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `65f955b77477` | tests: use measured faster serial execution for remaining ES configurations | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `0ff50f61cdc1` | tests: checkpoint 151k ES results with timeouts and GPU incident retained | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `10edd6fd72b4` | tests: checkpoint 200k ES configuration pairs without suppressing failures | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `42823e93a8d8` | test: retain serial GLES GPU timeout and reviewed recovery evidence | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `5b6f6ebe3fb9` | agx: legalize half derivative and signed half FMA sources | preserve from M4; origin commit is the review/rollback unit |
| M4 `d5fb5c438d44` | hk: retain native array gradient texel offsets | preserve from M4; origin commit is the review/rollback unit |
| M4 `7b71d55edd9d` | hk: reconstruct custom depth borders before shadow gather comparison | preserve from M4; origin commit is the review/rollback unit |
| M4 `55d3953fe189` | zink: retain acquired back buffers across pending swapchain recreation | preserve from M4; origin commit is the review/rollback unit |
| M4 `7c44167664f3` | test: record passing GLES failure retests and parallel swapchain regression | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `90db31cb21c4` | test: isolate proven long GLES cases in the fresh full rerun | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `a097f99be5e8` | test: retain new EGL heap-corruption crash and resume unrun coverage | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `2da009f46e1c` | zink: isolate and lock descriptor allocator ownership | preserve from M4; origin commit is the review/rollback unit |
| M4 `f9fc08ea091d` | test: retain descriptor allocator race reproducer and guarded evidence | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `14ff3904b9ef` | asahi: allow compiler replay to save exact shader binaries | preserve from M4; origin commit is the review/rollback unit |
| M4 `6a6f211bc514` | asahi: skip unused stack tokens when retiring scoreboard aliases | preserve from M4; origin commit is the review/rollback unit |
| M4 `9c41de45ddff` | test: retain slot compiler timing and fixed-build EGL GLES2 results | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `fe578033be91` | test: launch pinned OpenGL 4.6 regression rerun on latest Mesa | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `b2ad343ec67c` | test: complete pinned GL46 regression and revalidate CTS depth fix | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `33d0f72a0795` | test: launch fresh full ES32 regression on GL46-validated Mesa | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `c146cd50ff3c` | test: checkpoint 48454 fresh ES32 pairs and retain flush timing warning | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `0fdb13cec6c3` | test: retain ES32 GPU timeout snapshot and serial recovery evidence | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `d96d7c55d401` | test: complete ES32 fault dump archive and seven passing rechecks | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `1d3e4e60d15b` | test: record passing parallel ES32 repeat and reviewed coverage continuation | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `b3a5d13b32a6` | test: checkpoint 100357 ES32 pairs with incident history intact | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `7aa27102fd70` | tools: show individual ES CTS outcomes before worker groups finish | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `92d6f9d4b105` | tools: include single-case CTS files in live outcome counts | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `255cf0dc4b80` | test: checkpoint 151609 ES32 pairs and bounded copy-image continuations | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `c7e55a1c9163` | asahi: retain ES32 point-mode timeout and serial verification | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `f6d66afac0d7` | asahi: retain second ES32 tiling timeout and recovery evidence | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `c1a75ffd3ddc` | asahi: checkpoint ES32 regression beyond 200000 pairs | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `5fc6b0d374d4` | asahi: align ES32 comparison with frozen checkpoint rows | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `a2f0314d0e60` | asahi: checkpoint ES32 regression beyond 250000 pairs | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `f6ae18764acd` | asahi: checkpoint ES32 regression beyond 300000 pairs | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `e7eebe438994` | asahi: retain ES32 firmware stamp crash and reviewed recovery | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `235f36d98509` | Archive complete OpenGL ES 3.2 regression evidence | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `3e9cf1e7923a` | Archive G16 high-stamp ES 3.2 boundary trial | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `1748909ee29a` | Checkpoint repaired ES 3.2 past first GPU incident | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `b28f2c66cb48` | Checkpoint repaired ES 3.2 at 100357 indexed pairs | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M4 `08b19ec3a2e4` | Archive clean ES 3.2 handoff at 102857 pairs | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `f5460389b149` | asahi: enable the first experimental M3 G15S Vulkan compute dispatch | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `6960c6b0c20b` | asahi: enable first native G15S Vulkan graphics draw | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `7841b885b045` | asahi: verify every repeated M3 Vulkan draw against a poisoned image | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `b4d8693bf9ea` | docs: record isolated M3 integration and updated compiler smoke test | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `f96c021d90d6` | asahi: use M4 ten-slot USC helper tables for G15S | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `d45e493ab24a` | asahi: admit multidimensional G15S direct compute grids | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `6d03547de9a6` | docs: record GPU Wayland on the M3 native DCP panel | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `032840832b5a` | asahi: report VM bind errno and range for desktop diagnostics | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `b37aa1fc0313` | asahi: allow G15S native fragment sample loops used by Firefox | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `0e6bc9bc1889` | asahi: share G15S and G16G sampling and image robustness paths | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `4c606ebd3a00` | asahi: repair M3 native scratch and meta submission paths | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `ec29fc6bc9a5` | asahi: lower M3 sampler LOD bias independently of texture generation | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `a16b8f563086` | docs/asahi: record M3 CTS reference diagnostics and Plasma validation | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `2511cc83e75a` | asahi: fix M3 integer exports and preserve full-width funop inputs | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `49254bf8fd60` | asahi: preserve M3 LOD query bias and allow staged array gradient offsets | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `83a434ba09d5` | asahi: enable native M3 layer output for meta clears | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `4645b54bd595` | asahi: reserve the expanded M3 kernel render graph aperture | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `3f37a3a1ff93` | asahi: expose M3 native multisample formats with bounded tile geometry | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `9ef898a9b084` | hk: share M4 pooled native launch programs with M3 | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `35e2ff6d0bc4` | docs/asahi: qualify M3 launch pooling with 1000 CTS passes | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `872f0d775816` | hk: expose opt-in native uniform preambles on M3 shader ABI | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `1effcacac51c` | hk: expose bounded UBO prefetch variants for M3 experiments | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `aac930cb6c80` | hk: admit M3 to opt-in bounded Gen9 render compression | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `ec3f40a1c422` | hk: port M4 descriptor binding rollover fix to M3 | duplicate already present; retain M4 implementation and M3 evidence; origin commit is the review/rollback unit |
| M3 `28b3e32e9076` | hk: add optional present cadence and preamble register diagnostics | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `a04e764034b6` | hk: port dynamic UBO prefetch and validate 67 FPS M3 Vulkan race | duplicate already present; retain M4 implementation and M3 evidence; origin commit is the review/rollback unit |
| M3 `e89e47042fc9` | asahi: add opt-in native descriptor preamble and scaled-sample branches | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `aa40952c4c3c` | zink: use Honeykrisp WSI synchronization without a CPU present wait | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `23d64ab2c20f` | hk: add opt-in coalescing of identical fragment colour exports | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `b6183c37f01b` | hk: gate M3 early tiling on kernel capability and explicit opt-in | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `e7ea1453a024` | hk: cache command BO lookup and immutable draw options | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `562ccf096911` | hk: classify mixed fragment and compute barriers per graphics stream | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `e6fc3636537a` | hk: reuse immutable native pointer uploads within command buffers | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `f9e2ec876ce0` | hk: add bounded compact native descriptor prefetch | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `3c45227d5cbb` | hk: finish submit workers before destroying DRM queue resources | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `b38412649e00` | hk: diagnose presentation image acquisition alongside present timing | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `146a96154590` | asahi: preserve AGX3b widening for derivatives and fused multiply-add | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `f16324d56598` | asahi: print invalid operands without aborting rejection diagnostics | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `74d8fd45c644` | docs: record M3 GLES compiler crash regression evidence | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `cdaf409fa2cd` | docs: enable existing GLES1 support in the M3 EGL build | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `d113463d1fd7` | hk: preserve native array gradient texel offsets | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `5aa07efe9e1c` | zink: drop obsolete duplicate transform-feedback varying reservation | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `cc37cf1aeca7` | glsl: avoid cancellation in the acos builtin near one | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `bd4450e680a5` | zink: report geometry limits only when the stage is supported | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `f66e05a46689` | hk: advertise G15S multisampled depth and stencil sampling | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `d13fbb9bdfff` | glthread: restrict immediate-mode index unrolling to compatibility profiles | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `18ce05e07155` | hk: report G15S integer multisample texture and framebuffer support | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `f97fe4fb63d3` | hk: apply custom border comparisons to shadow gathers | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `fe9dfae65338` | hk: expose native G15 attachmentless multisampling | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `365bf208bbc6` | hk: preserve SNORM encodings in native image copies | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `30f558b4af2b` | hk: accept native clip output descriptors on M3 | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `2ed3bac695d2` | docs: record full M3 cull-distance regression | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `e419a3c40c68` | hk: preserve transform feedback metadata before replacing NIR | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `ed373e168402` | docs: qualify M3 feedback lifetime fix with all copy conversions | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `456123efc443` | asahi: lower native texture gathers in vertex shader stages | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `59cd35366c76` | docs: record M3 vertex gather crash fix and 525 passing regressions | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `37377c0f2740` | hk: qualify fragment storage and atomics on M3 | integrate from M3; reconcile changed consumers with M4; origin commit is the review/rollback unit |
| M3 `206e9cf0c0e0` | docs: record fragment storage qualification and prior-skip follow-ups | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |
| M3 `a9a2aa7e00df` | docs: classify retained M3 GLES framebuffer and line warnings | historical/evidence only; retain source records, no new execution claim; origin commit is the review/rollback unit |

## Production file coverage

M4-only files retain their complete producer/consumer contracts. Mixed files require hunk review before the integration unit is committed. Compiler changes require host semantics tests; ABI/command changes additionally require poisoned hardware output; resource/sampler changes require focused CTS; capabilities require per-device snapshots. All require the shared two-device candidate gate.

| File | Disposition |
| --- | --- |
| `src/asahi/compiler/agx3_pack_binary.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/compiler/agx3_slot_core.c` | preserve from M4 |
| `src/asahi/compiler/agx3_tex_core.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/compiler/agx_compile.c` | preserve from M4 |
| `src/asahi/compiler/agx_compiler.h` | preserve from M4 |
| `src/asahi/compiler/agx_nir_lower_g17p_combined_sampler.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/compiler/agx_nir_lower_sample_mask.c` | preserve from M4 |
| `src/asahi/compiler/agx_nir_lower_texture.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/compiler/agx_nir_opt_preamble.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/compiler/agx_nir_texture.h` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/compiler/agx_optimizer.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/compiler/agx_print.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/compiler/agx_register_allocate.c` | preserve from M4 |
| `src/asahi/compiler/agx_shader_replay.c` | preserve from M4 |
| `src/asahi/compiler/agx_spill.c` | preserve from M4 |
| `src/asahi/layout/layout.h` | preserve from M4 |
| `src/asahi/lib/agx3_compute_program_core.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/lib/agx_bg_eot.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/lib/agx_chip.h` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/lib/agx_device.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/lib/agx_device.h` | preserve from M4 |
| `src/asahi/lib/agx_device_virtio.c` | preserve from M4 |
| `src/asahi/lib/agx_linker.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/lib/agx_linker.h` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/lib/agx_nir_prolog_epilog.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/lib/meson.build` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/libagx/draws.cl` | preserve from M4 |
| `src/asahi/vulkan/hk_agx3_ioto_program_core.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_agx3_ioto_program_core.h` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_agx3_vdm_decode.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_agx3_vdm_decode.h` | preserve from M4 |
| `src/asahi/vulkan/hk_cmd_buffer.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_cmd_buffer.h` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_cmd_dispatch.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_cmd_draw.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_cmd_meta.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_cmd_pool.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_descriptor_prefetch.h` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_descriptor_set.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_device_memory.c` | preserve from M4 |
| `src/asahi/vulkan/hk_g17p_meta_copy_core.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_image.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_image_view.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_nir_lower_descriptors.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_nir_lower_minmax.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_nir_prefetch_descriptors.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_physical_device.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_query_pool.c` | preserve from M4 |
| `src/asahi/vulkan/hk_queue.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_queue.h` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_sampler.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_sampler_flags.h` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_shader.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_shader.h` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/asahi/vulkan/hk_wsi.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/compiler/glsl/builtin_functions.cpp` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/compiler/glsl/gl_nir_linker.c` | preserve from M4 |
| `src/compiler/nir/nir.h` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/compiler/nir/nir_lower_tex.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/gallium/auxiliary/util/u_threaded_context.c` | preserve from M4 |
| `src/gallium/drivers/zink/nir_to_spirv/nir_to_spirv.c` | preserve from M4 |
| `src/gallium/drivers/zink/zink_bo.c` | preserve from M4 |
| `src/gallium/drivers/zink/zink_context.c` | preserve from M4 |
| `src/gallium/drivers/zink/zink_descriptors.c` | preserve from M4 |
| `src/gallium/drivers/zink/zink_device_info.py` | preserve from M4 |
| `src/gallium/drivers/zink/zink_draw.cpp` | preserve from M4 |
| `src/gallium/drivers/zink/zink_kopper.c` | preserve from M4 |
| `src/gallium/drivers/zink/zink_pipeline.c` | preserve from M4 |
| `src/gallium/drivers/zink/zink_query.c` | preserve from M4 |
| `src/gallium/drivers/zink/zink_screen.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/gallium/drivers/zink/zink_state.c` | preserve from M4 |
| `src/gallium/drivers/zink/zink_types.h` | preserve from M4 |
| `src/gallium/include/pipe/p_defines.h` | preserve from M4 |
| `src/mesa/main/glthread_draw.c` | reconcile both; retain M3 adaptation and M4 later fixes |
| `src/mesa/state_tracker/st_extensions.c` | preserve from M4 |
| `src/vulkan/runtime/vk_pipeline.c` | preserve from M4 |

## Older dirty M3 work

The eight modified files in `mesa/m3-asahi` are preserved byte-for-byte. Patch SHA256 `89c1093ced37b1a85abd8063a19e5ce64c781cf0844595ad6cf316e582609c32` is archived under `build/m3m4-merge-20260923/older-m3-dirty.patch`.

- `hk_device.[ch]`, `hk_shader.c`: diagnostic NOMINMAX bypass, defer; disabling correct reduction is not a merge default.
- `hk_nir_lower_minmax.c`: branch explicit-LOD reduction setup, defer performance experiment; preserve current qualified lowering.
- `asahi-session.py`: older benchmark timing/seed control; historical harness, no production dependency.
- `desktop-launch.py`: historical linger/user-manager orchestration, retain untouched; use current target guards.
- `vulkan-trim-host.py`: optional reference profile capture handling; historical tool, retain unchanged.
- `zink_batch.c`: speculative pending-batch cap64; defer absent qualification, do not silently change scheduling.

## Baseline risks and gates

M3 retains 24 rendering warnings, two long-wait validation gaps, unsupported features, and a finite native-draw counter requiring controlled recovery. The prior GPUVA fault fix has useful regressions but no deterministic reproduction of its original kernel fault. M4’s latest repaired-kernel catalog remains partial, with earlier fault history retained. Neither is a certification baseline.

No original checkout or production prefix is changed for candidate tests. Use a new ARM64 build, matching generated host tools, isolated runtime prefixes and cold caches. Keep M3’s five-channel 95°C/45s guard and M4’s six-channel broker/120°C policy distinct. Both machines were idle with active display managers at task start.

The phase order remains source/build → host semantics → minimal outputs → focused bug/dossier CTS → frozen broad selections → desktop/application validation. Optimization flags retain their original per-target defaults; do not claim new performance until measured.

## First reconciliation checkpoint

`d6e2ba9c7a0` compiler: all 294 AGX unit tests passed, including M3’s added
widening and vertex-gather regressions, plus indexed graphics/compute sampler
lowering. These are fresh host tests, not GPU results. Logs are in
`build/m3m4-merge-20260923/{agx-tests-01,indexed-samplers-01}.log`.

Reviewed overlaps: M4 already implements the gather-stage fix, gradient-offset
preservation, early XFB metadata copy, and safe operand refusal printing. Retain
M4's implementations and add M3's nonduplicate tests/evidence. Retain M4's
compareEnable handling in software sampler state. M3's custom shadow-gather
border path preserves interior hardware comparisons and normalized-depth
clamping; keep it explicitly G15S-specific until equivalence with the M4
implementation is demonstrated. M4's compressed 1D copy admission stays intact;
M3 retains its narrower optional compressed-copy domain.

LOD policy: both Gen4 and Gen4-HAL200 load API bias in software. Preserve Gen4's
native biased-query operand and HAL200's reconstructed query clamp/mip result.
The early reduction-query/tap bias filter is explicitly selected for Gen4;
HAL200 retains its prior behavior pending numerical qualification. The generic
NIR hook remains because it expresses this precise internal-operation exclusion.
Gen5 hardware bias remains unchanged. This is an explicit conservative resolution
of the plan's sampler dossier, not an inference from common texture generation.

M3’s Honeykrisp CPU-present-wait removal is restricted to deviceID 0x6030.
M4 retains its prior implicit-sync setting. New optional preamble, compact
prefetch and compression flags remain off by default. Compiler-option keys
include bits 40–43 together with all producers/consumers of the optional layout.
The copied historical reports identify their original source revisions; none is
claimed as execution of the merged candidate.


## Hardware and host checkpoint, 2026-09-23

Candidate `c39d8a0f5b6` was built once for ARM64 and the same Vulkan,
Gallium, EGL, GLX and GBM libraries installed in isolated prefixes on both
machines. Both completed compute readback and 32 poisoned offscreen draws;
the rendered checksum was `ad5a84277c930c65` on each. Each device's full
Vulkan profile matches its production baseline except build/cache UUIDs and
driverInfo. Different devices retain different pipeline-cache identities.
All 153 previously repaired M3 GLES cases/configurations passed on both
machines, retaining their original global indices. This is focused coverage,
not a complete new conformance run.

The source/encoding verifier reproduced 508,700 scalar table values and all
1,494 retained instruction vectors. The broader Asahi host suite exposed ten
failures that reproduce on the untouched M4 source; their exact comparison
is archived in `host-baseline-comparison.json`. An obsolete negative G15S
resource-program expectation was corrected with positive resource-word
assertions and retained unsupported-device negatives.

Broader GLSL host testing exposed a real imported bug: the direct acos
polynomial compared vector operands against scalar zero. M4 baseline's
`general_ir_test` passed while the candidate asserted in half-vector builtin
initialization. `581916dcd95` fixes the comparison type; all 45 GLSL IR tests,
NIR, sampler-type and iterator tests pass. The rebuilt package also passes
compute and 32 poisoned renders on each machine. Hardware requalification
continues against that new immutable prefix; the first package is retained.

Raw evidence is `build/m3m4-merge-20260923`, with versioned manifests,
build logs, per-command actual library mappings, profiles, thermal/watchdog
records and frozen selections. One initial M3 collection error missed a very
short vulkaninfo mapping sample after successful GPU probes, so the guard
retained recovery and the machine was deliberately rebooted. The collector
now also accepts the loader's actual library-initialization trace. This was
a harness incident, not a GPU fault; both attempts remain archived. An M4
wrong-manifest baseline attempt and a missing optional provenance-field
harness error are likewise retained alongside successful corrected runs.

At `581916dcd95`, the repeated repaired GLES selection completed 153/153
Pass on each machine. The archived M4 experiment helper also now reflects
the existing workspace guard settings (CPU ceiling and provisional thermal
policy) and display-fault classification used for these runs.


## Native Vulkan and retained compiler replays

The deterministic 346-case Vulkan dossier run on `581916dcd95` completed:
M3 268 Pass / 78 NotSupported, M4 303 Pass / 43 NotSupported. No Fail,
crash, timeout or GPU/guard fault. This includes all 190 native texture LOD
queries on both, 12 dynamic-UBO cases on both, all retained M3 repair cases,
8 geometry passes on M4 (all unsupported on M3), and separate sparse,
tessellation, timeline, command-reuse, cache and pipeline-binary witnesses.
`vk-dossiers-581-comparison.json` preserves each family's outcomes. The
initial evenly-spaced reduction witnesses all required unsupported formats;
they do not qualify sampler reduction and require a supported-format followup.

M4 execution passed, but the first summary collector required an optional
classification field absent in these M3-style records. The results and guard
report were recovered directly from the retained experiment without rerunning
or modifying outcomes. Optional classification now reports `unclassified`.
Earlier local argument/schema validation attempts never armed hardware.

All 32 retained Factorio compiler inputs produce identical machine code with
M4 baseline and merged compiler. These are explicitly **adapted host replays**:
the normal capture ABI guard correctly rejected the old fingerprint. The only
changed fingerprint input is the nonserialized `nir_lower_tex_options` callback
in nir.h; all other hashed sources, including serialization and shader-key
layout, are identical. A retained script asserts those exact source identities
before changing only bytes 12..76 (the ABI hash) in new copies. Original NIR,
keys and captures are untouched. This does not establish cross-device cache
compatibility, new GPU execution or a performance gain.

Full frozen campaigns are now running in independent new directories:
`m3-gles31-581` (280,532 pairs) and `m4-gles32-581` (326,602 pairs). No old
outcomes or repair overrides were copied. New failure/guard incidents stop
for review. Indexed baseline comparisons additionally expose Pass→NotSupported
regressions. Completion and desktop promotion remain pending.


## Extended contracts and frozen-coverage checkpoint

The second M3 native selection contains 85 cases: 41 Pass / 44 NotSupported.
It closes the initial reduction gap with 12 RGBA8 reduction passes, plus
8 cache merges, all 5 invalid/concurrent cache tests, 4 custom depth-border
gather passes and 8 single-queue compute/graphics ordering passes. Compute-only
queue and unsupported component-swizzle restrictions remain explicit skips.
M4 execution of this additional selection is pending its current campaign.

The two GLES2 deliberate-wait cases again time out on M3 at 38 seconds;
25 explicit 1200 ms waits plus calibrated rendering exceed the existing bound.
Each attempt was stopped and reviewed separately: healthy completed outer
guard, continuing retirement, no GPU/kernel/OOM fault, unchanged CTS source.
They remain Timeout at global indices 20873 and 20877, matching the baseline;
manual review records permit unrelated coverage, not a pass or a timeout waiver.
Other completed broad coverage has no newly observed regression so far.

`production-dispositions.json` records the final content hashes of the 76
reviewed production files (34 M4 retained, 22 M3 adopted, 20 reconciled).
Helper snapshots add explicit versioned-prefix selection and rollback backups,
plus actual candidate mapping checks for desktop/app probes. Those deployment
helpers are prepared and syntax checked; desktop deployment/qualification has
not happened at this checkpoint. Original boot configuration remains intact.


## Advanced blend regression fixed in e325b646b16

The first broad M3 campaign stopped at three real regressions (global indices
65674–65676): indexed blend-equation overwrites with an advanced common equation.
Fresh serial reproduction: old M3 Mesa passes all 10 related cases; candidate
581 passes 7 and fails 3. Zink's new native advanced-blend CSO path from M4 was
unconditionally using advanced_blend_func, which the state tracker also fills
for the GLSL framebuffer-fetch fallback. M3 does not expose the native Vulkan
advanced-blend extension. Gate native CSO emission on the same Gallium capability
used to bypass GLSL lowering; do not add a chip-ID workaround or remove a GL
extension. M4 keeps native blending, M3 keeps its working GLSL fallback.

Committed fix e325b646b16 passes all 10 cases on each machine. The full 88-case
KHR GLES31 advanced-blend group gives M3 87 Pass / 1 NotSupported, M4 88 Pass.
Both also pass compute and 32 poisoned renders with the newly rebuilt identical
ARM64 package. The additional native contract group passes on M4 (see its raw
results); M3's earlier 41 Pass / 44 NotSupported is retained separately.

Two new M4 timing-calibration CompatibilityWarnings in the first broad run
(indices 65502 and 65504) pass serially on both the baseline and candidate.
The original warnings remain. New full campaigns run batches containing these
calibrated performance cases with one worker; frozen coverage/configurations
and the per-case/guard limits are unchanged.

Both earlier broad campaigns are stopped and preserved. M4's host supervisor
was paused while its current guarded child completed, then terminated only
with the watchdog broker idle. No active target guard was interrupted. Fresh
full catalogs, without inherited outcomes, now run as `m3-gles31-e325` and
`m4-gles32-e325`. Their artifact manifest hashes all seven regular shared
libraries, including the GBM backend. Normal desktop deployment is still pending.


## Replay preparation and content audit

The e325 content audit changes exactly one of the 76 production-file hashes
from the 581 checkpoint: the Zink advanced-blend gate described above.
`production-dispositions-e325.json` records the new checkpoint without replacing
the original manifest (33 M4 retained, 22 M3 adopted, 21 reconciled).
M4's additional 85-case Vulkan contract selection completed 44 Pass /
41 NotSupported on e325, no failures or guard incidents.

Replay helpers now retain the actual replay process mappings and reject a
missing selected Vulkan library. M3 additionally rejects a nonzero replay exit,
inner timeout, or missing third-frame image instead of leaving that decision
only to the caller. Syntax checks pass; updated hardware replay qualification
is still pending the broad campaigns. The historical M3 OpenGL capture has a
recorded replay fault, so it is not a known-good replay baseline. Retain that
limitation; use the qualified native Vulkan traditional-descriptor capture and
validate OpenGL separately with the real application (or a new successful
capture/replay). Do not label the known-broken trace a merge regression.


## Runtime-option coverage boundaries

The broad merged GLES campaigns use the candidate's normal compiler defaults
with nopromote and no optional game optimizations. In particular, the M4 guard's
ASAHI_MESA_DEBUG=nocompress remains in its actual CTS environment, whereas the
prior M4 production-option campaign explicitly enabled compression and several
prefetch/variant options. Indexed outcome comparison is a regression aid across
these recorded configurations, not a claim of identical optimization settings.
The normal desktop's existing options remain unchanged. A separately prepared
full 19,736-pair M4 GL46 campaign retains the prior production option set and
changes only package paths; it has not yet run. Original and corrected CTS
binaries remain separate. Application qualification also preserves each target's
actual installed game/desktop options.

M3's fresh e325 full campaign retains the same two reviewed deliberate-wait
Timeouts, at indices 20873 and 20877. Both guards completed with continuing
retirement, >12 GiB memory available, no OOM and no GPU fault. Raw attempts and
individual source/evidence reviews stay in their original campaign directories.


## Native M3 application replay, e325

The retained traditional-descriptor 0 A.D. Vulkan trace was replayed in
baseline/candidate/candidate/baseline order, with the existing game's options
(including early tiling, preambles, compression and broadcast coalescing).
All four guards completed. Actual loaded Vulkan mappings matched each chosen
library. Cold and warm replays produce byte-identical 3024x1964 screenshots,
SHA256 700d54176b6db1baa31ff841aad8de2bb5250f970064fede7144eb1b5ff38552.
Visual review confirms the scene, buildings, vegetation and HUD. Peak sampled
PMP temperature was 69 C. Durations include setup and image capture and are
not gameplay FPS or evidence of a speedup. Memory rebind, offscreen swapchain
and omitted capture pipeline cache are explicit adaptations.

The M3 campaign stopped at a normal segment boundary before these runs and
resumed afterwards with unchanged inputs/environment. The normal desktop and
game installations were not switched. M4 CTS continued independently. Full
coverage and real application/desktop qualification remain pending.


## Native M4 retained replay, e325

Four Factorio five-frame replays in baseline/candidate/candidate/baseline order
completed their guards with the selected Vulkan library mapped. Production
prefetch/variant, IOTO and compression options matched between runs. Frame 5
is byte-identical in all four at 2512x1552 (SHA256
12a681058bb24f962dbb8bed2a69a00e0858924cc138577884561ee27f8b2ba2).
Visual review confirms the menu's rendered factory background. Peak PMP was
63.75 C. Offscreen swapchain, memory rebinding and omitted capture pipeline
cache remain explicit adaptations; this is not real Factorio/desktop acceptance
or gameplay FPS. The host campaign supervisor alone was paused while its child
finished normally, then resumed after replay evidence was saved and broker idle.
No active guard was suspended. Full default-option GLES coverage continues.


## Real M3 game checkpoint: incomplete

Two merged OpenGL/Zink runs complete at 3024x1964 with actual candidate
Vulkan/Gallium/EGL/GBM mappings and the existing game flags. Ten-second frame
samples are 132.74 and 130.87 FPS; first old-Mesa baseline is 120.36 FPS. The
last baseline was externally interrupted; the user confirmed restarting the
machine. Its partial evidence stays incomplete (peak recorded 87.75 C).
A separately recorded post-reboot baseline completes at 120.82 FPS. This boot
change and the short samples prevent a clean paired speedup claim.

Image review found old/dim overlapping compositor frames before the reboot,
including on the original baseline. The post-reboot baseline image is clean.
Do not claim complete visual acceptance for the merged live game until its
fresh screenshot is reviewed; replay images remain clean and pixel-exact.
The compositor itself is still the old Mesa installation throughout these runs.

On the new boot, old-Mesa native Vulkan completes at 101.61 FPS with peak
86.25 C. The following merged native Vulkan attempt loses SSH connectivity;
target evidence is not yet available and no GPU/thermal root cause is inferred.
This is separate from the earlier user-confirmed interruption. Further M3
application tests and CTS stay stopped. M4 continues its independent guarded
campaign. The raw/compact intermediate record preserves both interruptions.
