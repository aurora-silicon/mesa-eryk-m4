# Paired 100,000-case preamble regression

The user requested a bounded 100k Vulkan 1.4 retest after the rendering checkpoint.
Each GPU runs the same frozen catalog range [0,100000), preserving original global
indices. This ordered range mainly covers API and copy/blit tests; it is not a
random sample or a full conformance run. Capability skips remain outcomes.

- CTS vulkan-cts-1.4.6.2, pinned source f6a29701220f34dd1407513bfe80d74ca7b392ce.
- Catalog SHA256 72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.
- Selection SHA256 4c29a8068efa1552aa87d8377f539c20b278dd1870461922c25f34e336cf1cdb.
- Final preamble-publication binary b656c92fb83e0839698ade836e18aa0c41fb805d9013dba5f516bfe4453e09b0.
- HK_M4_NATIVE_PREAMBLE=true and AGX_NATIVE_MEMORY_PREAMBLE=true;
  direct-buffer shortcuts and scaled-format branching remain disabled.
- Ordinary constant promotion; ASAHI_MESA_DEBUG=nocompress as in the paired
  regression/replay profile. This does not qualify untested compression paths.
- One worker per GPU, both machines run concurrently. Guarded segments retain
  60s watchdogs, thermal telemetry, per-case bounds, memory floors and first
  failures; no automatic retry hides failed or interrupted outcomes.

Local campaign: build/cts-preamble-100k-20260929/{m3,m4}. Each has immutable
configuration, pinned identities, progress.json, results.sqlite3, failures.tsv
and per-segment evidence. The controller now supports bounded ordered ranges and
explicit pinned compiler flags; offline tests cover range completion and resume,
configuration changes, and rejecting stored results outside the range. Sources,
selection and receipts are retained in the private local benchmark repository.

Execution is ongoing. This initial record does not assert 100k completion or
any Vulkan certification. Normal desktop driver installations are unchanged.
