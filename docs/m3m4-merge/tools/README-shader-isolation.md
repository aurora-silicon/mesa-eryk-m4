# Capture-only shader isolation

`shader-bisect-m3.py STATE --prefix PREFIX --sha256 SHA --reference FRAMES`
executes one bounded guarded replay with a diagnostic Mesa that calls
nir_shader_bisect_select() around the suspected compiler transformation.
`--compiler-debug nopromote` pins the exact flags in the state and replay.
It retains selections and exact pixel outcomes; diagnostic checks never qualify
performance results. Preserve an interrupted step log under a distinct name
before retrying; failed execution cannot advance the candidate state.

`gfxrecon-extract-shaders.py TRACE METADATA.jsonl OUTPUT` extracts SPIR-V offline
from selected gfxrecon-convert JSONL calls. Retain vkCreateShaderModule,
vkCreateGraphicsPipelines and vkCreateComputePipelines records. Inline stage
VkShaderModuleCreateInfo pNext data is common in pipeline-library captures:
looking only for standalone shader-module creation can silently miss all shaders.
The extractor rejects empty selection and unsupported block compression, checks
module sizes, and validates/disassembles every module with SPIR-V Tools. It
handles uncompressed or LZ4 function blocks and never changes the input trace.
The manifest records trace and module SHA256, pipeline/stage or module identity,
and the original block index. The Satisfactory DX11 capture extracted 1,020
validated inline shader modules; match SPIR-V BLAKE3 to NIR source_blake3 for
compiler bisection. Game shaders remain in the private benchmark repository.
