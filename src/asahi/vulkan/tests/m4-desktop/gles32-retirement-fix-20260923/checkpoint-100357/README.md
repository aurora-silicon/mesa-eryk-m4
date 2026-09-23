# Repaired-kernel ES 3.2 checkpoint: 100,357 pairs

The ordinary repaired kernel module completed segments 0–40 of the original
frozen, ordered 326,602-pair catalog. Recorded outcomes: 97,977 Pass, 2,348
NotSupported, 15 QualityWarning, 17 CompatibilityWarning, and zero Fail,
Timeout, or Crash. Exact indexed comparison with the baseline shows seven
original Fail cases now Pass, one original CompatibilityWarning now Pass, and
no worsening outcomes. The other baseline failures and timeout occur later in
the catalog and have not been revisited in this full rerun yet.

The raw indexed results, guarded-run summary, installed binary hashes, and
baseline comparison are compressed here. The `results.json` SHA256 in the
manifest identifies the uncompressed original. The local campaign retains
all individual segment logs and thermal/watchdog evidence. This is a partial
checkpoint, not completion of the full rerun.
