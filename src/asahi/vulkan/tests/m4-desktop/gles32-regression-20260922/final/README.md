# Final full-catalog ES 3.2 result

The raw 326,602-pair run finished with 316,008 Pass, 10,552 NotSupported,
15 QualityWarning, 18 CompatibilityWarning, 8 Fail, and 1 Timeout. Every
original failing or timed-out case passed a separately recorded recheck on the
same installed driver. The one new flush/wait CompatibilityWarning at global
index 20,875 passed an isolated, guarded recheck in 41.66 seconds (the original
warning took 38.18 seconds). Raw outcomes were not rewritten by rechecks.

`integrity.json` records an exact comparison of every indexed result against
the frozen session case lists, plus guard counts and peak observed temperature.
`results.json.gz` is one raw outcome per catalog pair. `attempts.json.gz`
preserves repeated/canceled attempts; `summary.json.gz` summarizes all 304
main guarded runs. `comparison.json.gz` compares to a mixed historical partial
baseline, with provenance in the parent directory. Results were recorded with
the original CTS and pinned installed Gallium/Vulkan hashes in the parent
directory's `installed-hashes.json`.

Two tiling timeouts and one firmware stamp-order crash remain unresolved. Their
reviews and sealed fault evidence are in the parent incident directories. The
point-mode per-case timeout and 900-second batch deadlines are reviewed in the
parent timeout and campaign records. This run is development evidence, not a
Khronos conformance or GPU stability claim.
