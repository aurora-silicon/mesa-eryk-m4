# Vulkan regression fixes — 2026-09-19

Full suite stopped at user request after393,459 terminal cases. Original
results are preserved in build/cts-vulkan14-regression-20260919 and
VULKAN_14_RETEST_FAILURES.tsv; they have not been replaced with passing reruns.
Do not resume the full suite automatically.

| Regression | Fix | Verification |
| --- | --- | --- |
|20 RGB8/RGB32 attachment-clear assertions|dc7b9d01842: pack padded RGBA storage texels|20/20 pass|
|2 descriptor-copy rendering failures|09d67eda571: advance independently across aligned source/destination bindings|289/289 descriptor-copy tests pass|
|2 packed-float format-property failures|ab3e191a47d: support2x/4x packed-float attachment layouts consistently|Both properties pass;400 clear cases pass, including44 MSAA cases;188 unsupported|
|2 stale host-test byte fixtures|2bc8abf8297: expect completion-slot wait mask4, matching eef4e004551|7009 IOTO +19267 uniform-program checks pass|

Combined candidate validation:903 cases,711Pass,188NotSupported,4QualityWarning,
zeroFail/Crash/Timeout. Normal installed-driver verification: all24 originally
failing/crashing cases PASS. These are targeted checks, not a completed full CTS.
The four layout allocation warnings remain: shared reference-counted layouts
use Mesa's device allocator and DEVICE lifetime; per-create fault injection sees
no calls. The common runtime documents this intentional lifetime choice.

Installed /opt/asahi-m4-port/lib/libvulkan_asahi.so SHA256:
b43300b15e16c0aed394d7aa1f31b6df84eee645c0ffc7796b32c8fe0b571baa
Code commit2bc8abf8297. Atomic replacement preserved existing desktop mappings;
new processes use the fixed library. Backup/deployment manifest retained under
/root/cts-regression-fixes-20260919/. Kernel and native FEX thunks unchanged.
CTS stays on the documented pre-existing corrected regression binary
(deqp-vk-seed30-scaled-blit); no test source or threshold was changed.

Each hardware validation used one worker,60-second watchdog and six-channel
thermal monitoring with95°C cutoff. All guards passed; final installed check
peaked84.25°C. The old parallel attempt's memory exhaustion was a separate
harness configuration problem, not a demonstrated new GPU fault. Both full and
bulk runners now default to one worker; higher parallelism requires a
memory-qualified selection. No claim of fixing a kernel allocation leak.

Evidence and exact lists: build/cts-regression-fixes-20260919/.
Source-side retained evidence:
mesa/m4-asahi/src/asahi/vulkan/tests/m4-desktop/cts-regressions-20260919/.
