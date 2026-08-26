# M3-M5 / A18 Pro driver matrix

Branch-local ledger. **IV** = implemented and host-validated, **RE** = reverse
engineered/modelled, **FC** = execution remains gated. This is not a hardware
support claim.

| Mesa chip | Product | ISA pack | Descriptors | Command stream | Vulkan / D3D12 | Linux DRM |
|---|---|---|---|---|---|---|
| G15G | M3 | **IV** AGX3b | **IV** Gen9/Gen4 | **IV/RE** records; production pending | **IV/FC** host paths | **RE/FC** classic bring-up |
| G15S | M3 Pro | **IV** AGX3b | **IV** Gen9/Gen4 | **IV/RE** records; production pending | **IV/FC** host paths | **RE/FC** classic bring-up |
| G15C | M3 Max | **IV** AGX3b | **IV** Gen9/Gen4 | **IV/RE** records; production pending | **IV/FC** host paths | **RE/FC** classic bring-up |
| G15D | M3 Ultra | **IV** AGX3b | **IV** Gen9/Gen4 | **IV/RE** records; production pending | **IV/FC** host paths | **RE/FC** classic bring-up |
| G16G | M4 | **IV** AGX3b | **IV** Gen9/HAL200 | **IV/RE** records; production pending | **IV/FC** host paths | **RE/FC** classic bring-up |
| G16S | M4 Pro | **IV** AGX3b | **IV** Gen9/HAL200 | **IV/RE** records; production pending | **IV/FC** host paths | **RE/FC** dual bring-up |
| G16C | M4 Max | **IV** AGX3b | **IV** Gen9/HAL200 | **IV/RE** records; production pending | **IV/FC** host paths | **RE/FC** dual bring-up |
| G17P | A18 Pro | **IV** AGX3b | **IV** Gen9/HAL200 | **IV/RE** records; production pending | **IV/FC** host paths | **IV/FC** channel ABI |
| G17G | M5 | **IV** AGX3c | **IV** Gen10/Gen5 | **IV/RE** records; production pending | **IV/FC** host paths | **RE/FC** SKSM bring-up |
| G17S | M5 Pro | **IV** AGX3c | **IV** Gen10/Gen5 | **IV/RE** records; production pending | **IV/FC** host paths | **RE/FC** SKSM bring-up |
| G17C | M5 Max | **IV** AGX3c | **IV** Gen10/Gen5 | **IV/RE** records; production pending | **IV/FC** host paths | **RE/FC** SKSM bring-up |
