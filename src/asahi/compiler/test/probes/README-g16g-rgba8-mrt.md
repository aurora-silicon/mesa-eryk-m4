# Two RGBA8 render targets

SuperTuxKart race replay reaches a pair of RGBA8 targets in an8-byte sample,
with tile offsets0/4. The previous admission accepted only two packed HDR
formats or RGBA16F+R8. The existing two-target clear/reload interface also applies
to RGBA8; retain both vec4 clear values and both native PBE descriptors.

The adjacent BTNF changes both attachment format IDs to3 and component counts
to4 in the retained two-target native template (partition0, layout2, mask3).
Metal32023.917 targeting applegpu_g16g emits120 bytes identical to the packed
HDR pair: both IMGW selectors are0x75. Artifacts build/stk-native-rgba8-mrt/.
The strict EOT graph matcher now admits two U8NORM block stores at0/4 too.

Qualification2026-09-12: native EOT byte test passes;6814 Ioto tests pass.
The extended HDR oracle passes21 attachment readbacks covering clear/draw/blend/
reload, including six RGBA8 pair readbacks, on software and M4. M4 evidence
20260912T045847969837Z-dual-rgba8-attachment-oracle, peak54.5 C with watchdog
and thermal guards. ARM64 library SHA256
dce5175e550fd6ea03d811e59fd93eeb77cc9ef1a8ccbd54f8b3fa8a91554e6b.
The11-frame adapted STK race now executes but its image still differs greatly
from software. Do not interpret the attachment oracle as complete game support.
