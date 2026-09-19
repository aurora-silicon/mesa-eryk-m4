# G16G compute visibility barriers

Native CommandEncodingNext::encodeBarrier selects distinct four-byte encodings
when its eAGXBarrierVisibility argument is nonzero. Implement only the two
witnessed G16G B0 words, keeping other chips and incomplete bit combinations
closed. Decoder coverage: 3971 host checks, including independent native words.

HK_M4_CDM_VISIBILITY_BARRIER=true uses that form for explicit cache operations
and at CDM stream closure. Default false while qualifying. The separate
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
