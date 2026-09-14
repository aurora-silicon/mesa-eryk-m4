# Ordered CTS batch 1: [0, 100000)

Execution in progress; this is not a final 100k result or conformance claim.
Catalog SHA256: 72c1bb9d2f9f0425ab8a4f570d78abdc1d3a34b82173ec76b772dedc477526bf.
The ten prepared adjacent 10k lists exactly reconstruct the frozen batch.
Twenty workers, groups of 50, 60-second case timeout, 900-second segment bound.
Each segment uses the mandatory watchdog/thermal guard. No outcome filtering.
The CTS binary contains previously recorded reference corrections.

First 10k outcomes: 3927 Pass, 6072 NotSupported, 1 Timeout.
`dEQP-VK.api.command_buffers.trim_command_pool` reached 60.056313 seconds.
The runner canceled its peers after that timeout; these are preserved as
Interrupted rather than fabricated independent crashes. After health inspection,
only the 4521 incomplete cases were resumed. The timeout was not retried.

Evidence: 20260914T215100954901Z-cts-ordered-b1-s01 and
20260914T215354846204Z-cts-ordered-b1-s01-remaining1. GPU remained healthy;
maximum exposed temperature 59.75 C. Original results and all interrupted
attempts are under the workspace build/cts-vulkan14-ordered directory.

30k checkpoint: 14580 Pass, 15395 NotSupported, 24 Fail, 1 Timeout.
All 24 correctness failures cover the stencil aspect of D16S8/D24S8 MSAA
copies at 2x/4x across whole/partial/array variants. See stencil-failures.json.
No driver crash/reboot; peak exposed temperature 60.25 C. Execution continues.
