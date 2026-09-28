# Native timing counters and 0 A.D. preamble experiment

M4 kernel 8f1de23c1e73, module 8b06d1175962a83b8913d0e77aa8198f84dbea3265d4e9e0391b46ee0822399e.
M3 development 7450b5bab12a, module 950a52f4b8a0188cf65b9ad2db12e427110dffb7a03fb8a47eb0a93ddb85d474.
Published M3 adaptation 07dc2de65f54 builds separately; not the deployed module.

All three source builds and shared concurrent-reader test pass. Both deployed
modules boot the normal desktop and pass 178 rendering/memory CTS cases, with
six NotSupported each. Timing snapshots are healthy with verbose logging off.
No firmware or scheduling changes. Counters include global clients, command
initialization and overlapping native retirement spans; NOT utilization.
M3 retirements may contain batches; commands count each command separately.
Missing timestamp samples are visible; readers report contention, not spin.

The retained 4K native Vulkan 0 A.D. replay has exact matching reference images
with both the baseline and optional read-only preamble. Three warm samples:

| GPU | Baseline median | Preamble median | Improvement |
|---|---:|---:|---:|
| M3 Pro | 42.1005 FPS | 58.8020 FPS | 39.7% |
| M4 | 27.2025 FPS | 38.4684 FPS | 41.4% |

Same capture b9002a6ff89bcc9a0bd1dcc994018363b50772f056ba8a9f61fde5662ebe2962,
Mesa 2e2f0cb06def7e6a7407059cb4da2bd295a486965e57dd60280908f61ca2b688,
30 GPU-completed measured frames. Candidate environment adds
HK_M4_NATIVE_PREAMBLE=true and AGX_NATIVE_MEMORY_PREAMBLE=true. Existing
nopromote benchmark policy remains identical between samples. No application
or resolution changes. Exact records, clocks, thermal traces and screenshots
are in the local benchmark repo sessions roadmap-20260928-timing-counters and
roadmap-20260928-preamble. Individual scores and peak temperatures are retained
in 0ad-preamble-comparison.json. This qualifies this replay, not all applications
or global default enablement. Broader 14,741-case UBO, robust access and
binding-copy validation is running on both GPUs before considering defaults.

M4 shader-profile diagnostic finds frequent fragment shaders with 1,000-1,200
instructions and 112-128 registers, no reported scratch. Counts are bindings,
not executed pixels. Its quiet baseline timing records about 1.03 s summed
fragment time and 0.034 s tiling, including restoration. Overlapping stage gaps
cannot be summed as idle GPU time. Record actual frame time independently.

The first external timing wrapper was rejected by replay.py's direct-guard
parent check before any replay. Retained as process_failure, not a GPU fault.
The qualified replay script now takes optional debugfs snapshots immediately
outside its child timing interval, retaining its original parent guard check.

## Broader preamble validation completed

Both GPUs finish all 14,741 selected frozen-catalog cases: 10,445 Pass and
4,296 NotSupported each, no failure/crash. Three complete families selected
without result/capability filtering: ubo, robustness.buffer_access and
binding_model.descriptor_copy. Indexed outcomes and catalog/selection hashes
are retained in preamble-ubo-results.tsv and preamble-ubo-validation.json.
This is substantial affected-feature coverage, not full Vulkan conformance
or qualification of every application. Options remain opt-in for now.
