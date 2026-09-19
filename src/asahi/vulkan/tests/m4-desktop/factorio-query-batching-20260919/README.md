# Adjacent native query-copy batching (experimental, default off)

HK_M4_BATCH_QUERY_COPIES records exact destinations for internal result-copy
shaders. Only streams where these dispatches account for every command acquire
a complete footprint; normal CDM merging then preserves timestamp and explicit
memory-dependency boundaries. Every existing native dispatch cache flush stays.
No application shader matching, query reset bypass, or app settings change.

Native build passes. Guarded J713 evidence:
- 20260919T054734275566Z did not reach hardware: experimental M4 enable flag
  missing in the launch command. Corrected in the following run.
- 055332015190Z: 12 separate-call timestamp cases pass (32/64-bit, WAIT,
  availability, unwritten queries, padding and independent fill preservation).
  Logs prove four API calls -> one CDM with merged=3. Peak 64.5 C.
- 055629997151Z: 64 host-reset available queries pass exact CPU/GPU comparisons;
  destination attachment capacity bounds groups to 16 dispatches (four CDMs).
  This is not a single 64-dispatch merged stream. Peak 63.75 C.
- 055401693099Z: original Factorio replay TIMES OUT on job 6279, after command
  26 retired. Compute stamp 488900 remained unretired; prior stamp 488800
  published. Guard retained a sealed fault dump and recovered by reboot.
  The original trace contains GPU resets; that is a difference from the
  passing controls, NOT proof of the failure's cause. Keep option disabled.

Profiler extension records BEGIN/SUBMIT boundaries for HK_M4_SHADER_PROFILE.
The helper pairs complete per-address submission sequences to G16G_PASS records
in an explicitly selected ASID; missing counts remain a lifetime union. Capture
054302402085Z matched 8,401 passes. This fixes address reuse ambiguity, not game
performance. Profiling is disabled during ordinary throughput measurements.

Full CTS remains deferred. Sustained live-menu 60 FPS remains unmet.
