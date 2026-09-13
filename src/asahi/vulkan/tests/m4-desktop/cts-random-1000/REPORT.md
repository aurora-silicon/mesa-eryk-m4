# CTS sample: 1000/1000 completed

| Outcome | Count | % of selected sample |
| --- | ---: | ---: |
| Crash | 2 | 0.2% |
| Fail | 47 | 4.7% |
| NotSupported | 151 | 15.1% |
| Pass | 800 | 80.0% |

Peak recorded temperature: 59.0 C.

Unsupported cases are not passes. Timeouts use the recorded per-case bound.
Process crashes are not necessarily GPU resets. Inspect retained logs for attribution.
This sample is not a full Vulkan conformance result.

Pass rate excluding unsupported cases: **94.23% (800/849)**.

One GPU reset occurred within the 47 failures. Two process aborts were
render-pass input-attachment assertions; they did not reset the GPU.
Seed: 20260919. Only Windows-specific and Android WSI cases were excluded
before selection. The separately identified corrected historical CTS binary
and driver were unchanged; there were no retries or sample replacements.

| Category | Pass | Unsupported | Fail | Crash |
| --- | ---: | ---: | ---: | ---: |
| api | 111 | 77 | 1 | 0 |
| binding_model | 94 | 0 | 0 | 0 |
| clipping | 0 | 0 | 2 | 0 |
| draw | 2 | 0 | 1 | 0 |
| fragment_operations | 1 | 0 | 0 | 0 |
| glsl | 74 | 4 | 2 | 0 |
| image | 32 | 1 | 15 | 0 |
| memory | 5 | 0 | 0 | 0 |
| pipeline | 280 | 34 | 8 | 0 |
| renderpass | 37 | 0 | 7 | 2 |
| sparse_resources | 0 | 2 | 0 | 0 |
| spirv_assembly | 10 | 0 | 0 | 0 |
| ssbo | 9 | 0 | 0 | 0 |
| synchronization | 66 | 0 | 7 | 0 |
| tessellation | 0 | 0 | 2 | 0 |
| texture | 11 | 0 | 0 | 0 |
| ubo | 6 | 0 | 0 | 0 |
| ycbcr | 62 | 33 | 2 | 0 |
