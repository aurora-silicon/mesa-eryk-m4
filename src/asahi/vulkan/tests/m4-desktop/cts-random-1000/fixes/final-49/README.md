# Final regression of the original failures and crashes

**49/49 Pass, zero failures or crashes**, on one boot with unchanged binaries
across all five guarded batches. Peak monitored temperature **59.25 C**;
60-second watchdog and six-channel thermal monitoring active for every batch.

This retests all original **47 Fail and two Crash** in the frozen random
1,000-case sample. `baseline-mapping.json` maps every retest to its original
index and result. The original 800 Pass and 151 NotSupported were not rerun.
Unsupported cases are not passes; this is not a full conformance result.

`source.json` identifies the tested Mesa/kernel commits and exact runtime
library, CTS binary and guard hashes. `summary.json` records each retained
experiment, watchdog/thermal evidence and counts; `results.json` contains
actual CTS results, rather than inferred guard outcomes.

The CTS binary includes the previously documented corrections for invalid
subpass input aspects (original process crashes 0/848), zero-sized tessellation
render area (9), and reversed midpoint reconstruction weights (396). Their
patches and binary manifests are retained in the parent directory. These are
CTS fixes, not driver-support claims. The other failures were fixed in Mesa
or the driver, including original driver-reset case 6.

Production source ends at Mesa `360e3b04daa`; subsequent evidence-only commits
do not change the tested implementation. No diagnostic shader substitutions,
extra barrier override, CPU shader execution, or relaxed comparisons were used.
