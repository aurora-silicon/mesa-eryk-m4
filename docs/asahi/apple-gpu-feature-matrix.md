# M3-M5 / A18 Pro driver matrix

**IV** = implemented/host-validated; **RE** = modelled from artifacts; **FC** = live use remains gated.

| Mesa chip | Product | ISA pack | Descriptors | Command stream | Vulkan / D3D12 | Linux DRM |
|---|---|---|---|---|---|---|
| G15G | M3 | **IV/FC** AGX3b pack; residual forms gated | **IV** Gen9/Gen4 | **IV/FC** production decode/state; live emit gated | **IV/FC** Vulkan+D3D bridges, RT codecs; caps gated | **RE/FC** topology; power gated |
| G15S | M3 Pro | **IV/FC** AGX3b pack; residual forms gated | **IV** Gen9/Gen4 | **IV/FC** production decode/state; live emit gated | **IV/FC** Vulkan+D3D bridges, RT codecs; caps gated | **RE/FC** topology; power gated |
| G15C | M3 Max | **IV/FC** AGX3b pack; residual forms gated | **IV** Gen9/Gen4 | **IV/FC** production decode/state; live emit gated | **IV/FC** Vulkan+D3D bridges, RT codecs; caps gated | **RE/FC** topology; power gated |
| G15D | M3 Ultra | **IV/FC** AGX3b pack; residual forms gated | **IV** Gen9/Gen4 | **IV/FC** production decode/state; live emit gated | **IV/FC** Vulkan+D3D bridges, RT codecs; caps gated | **RE/FC** topology; power gated |
| G16G | M4 | **IV/FC** AGX3b pack; residual forms gated | **IV** Gen9/HAL200 | **IV/FC** production decode/state; live emit gated | **IV/FC** Vulkan+D3D bridges, RT codecs; caps gated | **RE/FC** topology; power gated |
| G16S | M4 Pro | **IV/FC** AGX3b pack; residual forms gated | **IV** Gen9/HAL200 | **IV/FC** production decode/state; live emit gated | **IV/FC** Vulkan+D3D bridges, RT codecs; caps gated | **RE/FC** topology; power gated |
| G16C | M4 Max | **IV/FC** AGX3b pack; residual forms gated | **IV** Gen9/HAL200 | **IV/FC** production decode/state; live emit gated | **IV/FC** Vulkan+D3D bridges, RT codecs; caps gated | **RE/FC** topology; power gated |
| G17P | A18 Pro | **IV/FC** AGX3b pack+SCS; RT lowering gated | **IV** Gen9/HAL200 | **IV/FC** production decode/state; live emit gated | **IV/FC** Vulkan+D3D bridges, RT codecs; caps gated | **IV/FC** ASC owner/order; PMGR gated |
| G17G | M5 | **IV/FC** AGX3c pack; residual forms gated | **IV** Gen10/Gen5 | **IV/FC** production decode/state; live emit gated | **IV/FC** Vulkan+D3D bridges, RT codecs; caps gated | **IV/FC** ASC owner/order; PMGR gated |
| G17S | M5 Pro | **IV/FC** AGX3c pack+SCS; RT lowering gated | **IV** Gen10/Gen5 | **IV/FC** production decode+Ioto emit; full submit gated | **IV/FC** Vulkan+D3D+Ioto+RT codecs; caps gated | **IV/FC** ASC owner/order; PMGR gated |
| G17C | M5 Max | **IV/FC** AGX3c pack; residual forms gated | **IV** Gen10/Gen5 | **IV/FC** production decode/state; live emit gated | **IV/FC** Vulkan+D3D bridges, RT codecs; caps gated | **IV/FC** ASC owner/order; PMGR gated |
