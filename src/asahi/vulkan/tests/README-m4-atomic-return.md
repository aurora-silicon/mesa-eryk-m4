# M4 device atomic return storage

2026-09-12, J713/T8132 G16G B0, firmware 25G83.

vkQuake's original 83-frame Vulkan capture submitted successfully but its world
was black. Frame59 dispatch47174 generated no indirect draw counts. Lavapipe
produced2373 indices across99 draw records. Resource dumps proved all six input
SSBOs matched; visibility and backface decisions matched for every surface.
A64-lane unique-address atomic-add probe then isolated unchanged counters and
an apparent return equal to the increment operand.

Apple's offline G16G same-buffer Metal witness contains:

- CLR at code+112: `0b000002`, destination kind4 (TPS), offset0, slot1.
- ATOMIC at+116: `6711540000800181004200006003`, return kind4, offset0, slot1.
- RLD at+130: `3c8009a700200000`, source kind4, offset0, destination GPR6,
  completion slot6, waiting slot1.

Offsets are within retained text-850.bin (entry+64). Full source, compiler
commands, archive hashes and sections are retained under workspace
_provenance/vkquake-reference-20260912/atomic-witnesses/same-buffer/.
This is offline compiler evidence, not macOS execution evidence.

The old encoder assumed an in-place GPR return, tied source/destination in RA,
and omitted TPS initialization, allocation and reload. The fix reserves a
reusable return word after existing scratch/spills, emits the native sequence,
and makes the reload carry the ordinary result dependency. AGX3 SPECTPR consumes
scratch bytes; AGX2's stack-word conversion is not its ABI. M4 uses the kernel's
owned USC private-memory pool, so userspace must not attach an AGX2 helper.

Hardware evidence retained in _provenance/experiments/:

-113921989442 divergent minimal:64/192 wrong before fix.
-114559388879 uniform control:128/192 wrong; failure is not divergence-specific.
-115257653196 native TPS return:0/192 wrong,54C peak.
-115814341509 independent input/result allocation:0/192 wrong,54C peak.
-120014104227 eight-operation regression:0/1024 wrong,54.25C peak. Tests add,
 exchange, compare-exchange (match and mismatch), unsigned min/max, and/or/xor.
 CPU oracle and host lavapipe agree independently. Inactive lanes remain intact.
-115358636166 original vkQuake resource replay: all99 indirect records exact,
 count sum2373,55.25C peak. World now renders. Exact frame60 mean channel error
 1.60484/255,max65,28521/480000 pixels above8, chiefly texture/lighting detail.
 This is not a claim of pixel-perfect rendering or native game qualification.

All hardware runs used the60-second watchdog, host SSH broker and thermal guard.
Slot-only, synthetic-wait and consumed-value-only diagnostics did not fix the
failure and are absent from the final implementation. No shaders were replaced
in the original replay and no Vulkan features were removed.

`m4-desktop/atomic-oracle.py --output NEWDIR` generates the integer regression;
compile probe.comp with glslangValidator, then use vk-buffer-oracle.c under the
mandatory hardware guard. M4_ORACLE_SOFTWARE=1 selects host lavapipe explicitly.
