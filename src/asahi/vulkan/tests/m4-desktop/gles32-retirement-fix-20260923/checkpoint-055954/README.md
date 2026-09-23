# Repaired-kernel checkpoint: 55,954 pairs

The full frozen ES 3.2 rerun has completed the first 23 guarded segments on
ordinary module SHA256 `afc871fb4273720d8825c644cf7823a10457ed9eb16a9e9a3f47303978357956`.
Raw results at this checkpoint: 54,247 Pass, 1,696 NotSupported, 10
QualityWarning, 1 CompatibilityWarning, zero Fail/Timeout/Crash. The exact
2,500-case four-worker segment 22 passed 2,466/34 Pass/NotSupported, and all
seven original image failures changed from Fail to Pass. No GPU incident was
recorded in this rerun of the segment. The baseline's `flush_wait` timing
warning became Pass; its `finish_wait` warning remains.

The results and comparison here are a checkpoint, not full validation.
Original raw failures and fault evidence remain in the separate baseline
campaign. The controller continues with the same frozen case indices and
guarded limits.
