# Seed 20260920 image format work

The frozen selection contains all 89 image-format/operation NotSupported cases
from the final 871 Pass / 129 NotSupported report. Original indices are in
selection.json; subset indices are in the new result files. CTS and its oracles
are unchanged.

RGB/BGR storage and scaled sampling: 20 Pass, 69 NotSupported, zero failures or
crashes with ten workers. GPU transfers retain API three-component byte layout;
optimal image storage is padded and sampled alpha is one. Scaled samples use
integer descriptors and float conversion; linear scaled filtering is not yet
advertised. Signed BGRA EOT uses the existing packed I8 word path.

Exact binary identities, guarded experiment path and timing are retained in
rgb-summary.json. Experiments used the 60-second hardware watchdog and mandatory
six-channel thermal guard. An initial newly enabled signed BGRA blit failed the
tile-layout allowlist; adding its I8 EOT path made that case and the complete
89-case subset pass or remain unsupported without failures.

Padded RGB attachments: 25 Pass, 64 NotSupported, zero failures or crashes.
The four RGB blend cases and BGR sRGB granularity now pass. Epilogs use the
logical API format for blending (including absent destination alpha), then
pad stores to the physical tile format. Physical layout and blend semantics
remain separate. See blend-results.json and blend-summary.json.

Packed 5:5:5:1: 37 Pass, 52 NotSupported, zero failures or crashes.
Vulkan R5G5B5A1/B5G5R5A1 map to low-alpha Gallium formats, which use native
channel layout 0x0d. Twelve more original cases pass, covering cube/array
sampling, mip filtering, blits and blending. Native PBE packing already
supports this channel layout; retain the generic component swizzles.

X8 D24: original case 428 now passes its shadow sampling oracle. Physical
depth uses float32, with the existing packed-D24 GPU transfer conversion.
Views, host-transfer restrictions, sample counts and depth bias follow the
existing D24S8 emulation. This is one-case evidence; a regression rerun follows.
