# G16G compute visibility barriers

Native CommandEncodingNext::encodeBarrier selects distinct four-byte encodings
when its eAGXBarrierVisibility argument is nonzero. Implement only the two
witnessed G16G B0 words, keeping other chips and incomplete bit combinations
closed. Decoder coverage: 3971 host checks, including independent native words.

HK_M4_CDM_VISIBILITY_BARRIER=true uses that form for explicit cache operations
and at CDM stream closure. Initially opt-in during qualification; enabled after the checks below. The separate
HK_M4_CDM_END_BARRIER switch isolates closure placement from encoding.

GPU query reset, fills, timestamps, then NO-WAIT CPU/GPU copies:
- Native wide visibility: all four results available, exact CPU/GPU values.
- Same candidate with option off: all four unavailable.
- Extra ordinary end barrier alone: still all four unavailable.

This fixes a reproduced visibility failure that blocked compute batching.
Do not describe the ordinary word as sufficient cross-agent publication.
The native five-frame Factorio capture with fill batching now passes every
pixel and query total, 6.070..6.207 ms complete GPU spans. This is not yet a
major speedup; 5 ms remains unmet. Full CTS is deferred.

The 13-mode exact query matrix passes, including the previously hanging WAIT
copy and 64 individually reset queries. Peak63C. Guarded execution only.

## Qualified default and safe batching

The native visibility form is now the default for G16G explicit cache
operations and stream closure. Track actual trailing visibility, preserve it
when joining streams, and allow memory-dependency merging only with that
operation still present and a complete bounded write ledger. Timestamp and
query-reset ownership boundaries remain. Fill-ledger batching defaults on
with wide visibility. Diagnostic overrides remain available.

A final compute stream's retained visibility barrier also satisfies the
command-buffer publication of earlier fragment-statistics atomics. Avoid
creating another firmware job just to repeat it. The native five-frame replay
passes exact pixels and all query totals at 5.984..6.108 ms. Its sixteen commands
replace the earlier eighteen; this is a modest gain, not the 5 ms target.
The exact statistics oracle passes odd edges, GPU copies and split submissions.
All 13 query modes and 8 targeted synchronization2 cases pass with batching.
Final built-default query validation and live qualification are recorded below.

The final built defaults pass all 13 modes without new option overrides
(162015 query-visible-default-matrix, peak 64.75 C). Live qualification follows.

Installed library SHA256 80e0c17d6fd020c625facd209976fcd6fd294280451fb3e8c4ce259439263954.
Normal menu initialized in 7.102 s game time; 120 s observation, 235 samples,
median 59.97 FPS, mean 58.799, minimum 33.55, 84.7% at least 59 FPS.
Peak PMP temperature 85.5 C. Bounded process stopped normally. This is not
a flat 60 FPS result. Previous installed library a02c6815 is retained for rollback.

## Optional host stamp coalescing

Kernel capability8/compute flag2 permits native wide-visibility streams to
defer intermediate host stamp flushes until the bounded packet's last compute.
FW execution dependencies and all query endpoints remain. GPU publication
precedes firmware timestamp writes, so streams with any API timestamp keep
their own flush. HK_M4_COALESCE_COMPUTE_STAMPS remains off by default.
165411 all13querymodes pass. 165434 pixels and counts exact,5.949..6.111ms;
no meaningful performance gain. Keep opt-in, not a normal-driver speedup.
