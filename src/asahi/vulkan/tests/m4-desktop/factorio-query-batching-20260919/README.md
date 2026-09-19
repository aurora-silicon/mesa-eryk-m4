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

Correction after baseline control: 055940493570Z also times out with batching
OFF. Comparing manifests shows both failed replay invocations omitted eight
qualified native-path options, including timestamp fanout/completion and
compute timestamp placement. Therefore neither failure establishes a batching
regression. The replay helper now defaults those options to the qualified
Factorio path, preserving explicit caller overrides. Corrected replay 060122708206Z PASSES with zero differing pixels against the
retained reference (PNG SHA256 88594c10407e243eb0617860b8f2abc1f06c0844ec67ddba3900875d7e36796e).
Peak 73.5 C; actual groups include 7/8 query copies. Live normal-size menu
060155329234Z PASSES bounded observation: last 12 samples average 42.39 FPS,
range 41.01--43.92, peak 91.25 C. Animated scenes differ across launches, so
this is not an isolated batching speedup. Current Zink issues only one query
copy per frame; this optimization is more relevant to the original capture.
Keep the option default-off pending wider qualification.

Selected compiler capture 060724464673Z retains exact inputs for 49d4b829
(full-screen composite, 78 instructions) and 67327449 (distortion, 146).
Both offline replays reproduce the executable hashes. The generic optional
capture filter does not replace shaders. Peak 89.5 C; no GUI input.
`factorio-standalone-observe.py --compiler-capture DIR` retains new bundles in
experiment evidence, with existing watchdog and thermal guards unchanged.
