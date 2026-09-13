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
