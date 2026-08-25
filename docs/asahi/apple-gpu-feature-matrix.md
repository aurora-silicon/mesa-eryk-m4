# Apple AGX / Asahi feature matrix

Branch-local implementation ledger. Rows follow `enum agx_chip` order.

Legend: **RS** released/supported; **IV** implemented and host-validated; **RE**
reverse-engineered/modelled; **FC** fail-closed or unavailable. Combined states
mean a bounded subset reached the first state while execution remains at the
second. “Host” means CPU tests/builds, not live hardware.

## Hardware and encoding axes

| Mesa identity | Marketing mapping | ISA / encoding | Compiler pack / decode | Texture/PBE descriptor | Sampler / heap element | Command stream / submission |
|---|---|---|---|---|---|---|
| G13G | M1 | AGX2 / `agx2` | **RS** full | **RS** Gen7, 24 B | **RS** Gen4, 8 B | **RS** classic |
| G13X | M1 Pro/Max/Ultra | AGX2 / `agx2` | **RS** full | **RS** Gen7, 24 B | **RS** Gen4, 8 B | **RS** classic |
| G14G | M2 | AGX2 / `agx2` | **RS** full | **RS** Gen8, 24 B | **RS** Gen4, 8 B | **RS** classic |
| G14X | M2 Pro/Max/Ultra | AGX2 / `agx2` | **RS** full | **RS** Gen8, 24 B | **RS** Gen4, 8 B | **RS** classic |
| G15P | A16 Bionic | AGX2 / `agx2` | **IV/FC** codegen only | **FC** unknown | **FC** unknown | **FC** unknown |
| G15G | M3 | AGX3 / `agx3b` | **IV** ALU/memory/SR/varying/CF/texture/tile | **IV** Gen9, 32 B | **IV** Gen4, 32 B | **RE/IV** classic CDM/VDM + draw packers |
| G15S | M3 Pro | AGX3 / `agx3b` | **IV** ALU/memory/SR/varying/CF/texture | **IV** Gen9, 32 B | **IV** Gen4, 32 B | **RE/IV** classic CDM/VDM + draw packers |
| G15C | M3 Max | AGX3 / `agx3b` | **IV** ALU/memory/SR/varying/CF/texture | **IV** Gen9, 32 B | **IV** Gen4, 32 B | **RE/IV** classic CDM/VDM + draw packers |
| G15D | M3 Ultra | AGX3 / `agx3b` | **IV** ALU/memory/SR/varying/CF/texture | **IV** Gen9, 32 B | **IV** Gen4, 32 B | **RE/IV** classic CDM/VDM + draw packers |
| G16P | Phone part; mapping ungrounded | AGX3 / `agx3b` | **IV** shared AGX3b subset | **IV** Gen9, 32 B | **IV** Gen4 HAL200, 32 B | **RE/FC** submission unknown |
| G16G | M4 | AGX3 / `agx3b` | **IV** ALU/memory/SR/varying/CF/texture/tile | **IV** Gen9, 32 B | **IV** Gen4 HAL200, 32 B | **RE/IV** classic CDM/VDM + draw packers |
| G16S | M4 Pro | AGX3 / `agx3b` | **IV** shared AGX3b subset | **IV** Gen9, 32 B | **IV** Gen4 HAL200, 32 B | **RE/IV** dual-stack CDM/VDM + draw packers |
| G16C | M4 Max | AGX3 / `agx3b` | **IV** shared AGX3b subset | **IV** Gen9, 32 B | **IV** Gen4 HAL200, 32 B | **RE/IV** dual-stack CDM/VDM + draw packers |
| G17P | A18 Pro | AGX3 / `agx3b` | **IV** shared AGX3b subset | **IV** Gen9, 32 B | **IV** Gen4 HAL200, 32 B | **RE/IV** dual-stack CDM/VDM + draw packers |
| G17A | A18-class; mapping unknown | AGX3 / `agx3b` | **IV** shared AGX3b subset | **IV** Gen9, 32 B | **IV** Gen4 HAL200, 32 B | **RE/FC** submission unknown |
| G17G | M5 | AGX3 / `agx3c` | **IV** ALU/memory/SR/varying/CF/texture | **IV** Gen10, 32 B | **IV** Gen5, 32 B | **RE/IV** SKSM CDM/VDM + Gen7 barriers/draw |
| G17S | M5 Pro | AGX3 / `agx3c` | **IV** ALU/memory/SR/varying/CF/texture/tile | **IV** Gen10, 32 B | **IV** Gen5, 32 B | **RE/IV** SKSM CDM/VDM + Gen7 barriers/draw |
| G17C | M5 Max | AGX3 / `agx3c` | **IV** ALU/memory/SR/varying/CF/texture | **IV** Gen10, 32 B | **IV** Gen5, 32 B | **RE/IV** SKSM CDM/VDM + Gen7 barriers/draw |

## Mesa API and kernel relationship

| Mesa identity | Gallium / OpenGL | Honeykrisp / Vulkan | D3D12 AGX / WDDM UMD | DXR | Linux DRM relationship |
|---|---|---|---|---|---|
| G13G | **RS** AGX2 | **RS** AGX2 | **RS** AGX2 + built UMD contracts | **FC** Tier 0; no HW RT | **RS** existing path |
| G13X | **RS** AGX2 | **RS** AGX2 | **RS** AGX2 + built UMD contracts | **FC** Tier 0; no HW RT | **RS** existing path |
| G14G | **RS** AGX2 | **RS** AGX2 | **RS** AGX2 + built UMD contracts | **FC** Tier 0; no HW RT | **RS** existing path |
| G14X | **RS** AGX2 | **RS** AGX2 | **RS** AGX2 + built UMD contracts | **FC** Tier 0; no HW RT | **RS** existing path |
| G15P | **FC** device axes unknown | **FC** device axes unknown | **FC** shader code only | **FC** Tier 0; no HW RT | **RE/FC** identity only |
| G15G | **IV/FC** Gen9 heaps; execution gated | **IV/FC** Gen9/Gen4 heaps; execution gated | **IV/FC** descriptors/tile/host DDI; no AGX3 execution | **IV/RE** Gen1 BVH/primitive helpers; no traversal | **RE/FC** identity/UAT/recovery/kick/ring preflight |
| G15S | **IV/FC** Gen9 heaps; execution gated | **IV/FC** Gen9/Gen4 heaps; execution gated | **IV/FC** descriptors + host DDI; no AGX3 execution | **IV/RE** Gen1 BVH/primitive helpers; no traversal | **RE/FC** identity/UAT/recovery/kick/ring preflight |
| G15C | **IV/FC** Gen9 heaps; execution gated | **IV/FC** Gen9/Gen4 heaps; execution gated | **IV/FC** descriptors + host DDI; no AGX3 execution | **IV/RE** Gen1 BVH/primitive helpers; no traversal | **RE/FC** identity/UAT/recovery/kick/ring preflight |
| G15D | **IV/FC** Gen9 heaps; execution gated | **IV/FC** Gen9/Gen4 heaps; execution gated | **IV/FC** descriptors + host DDI; no AGX3 execution | **IV/RE** Gen1 BVH/primitive helpers; no traversal | **RE/FC** identity/UAT/recovery/kick/ring preflight |
| G16P | **IV/FC** Gen9 heaps; execution gated | **IV/FC** Gen9/HAL200 heaps; execution gated | **IV/FC** descriptors + host DDI; no AGX3 execution | **IV/RE** Gen1 helpers; no traversal | **RE/FC** identity/HAL only; submission unknown |
| G16G | **IV/FC** Gen9 heaps; execution gated | **IV/FC** Gen9/HAL200 heaps; execution gated | **IV/FC** descriptors/tile/host DDI; no AGX3 execution | **IV/RE** Gen1 helpers; no traversal | **RE/FC** classic/recovery/kick/ring preflight |
| G16S | **IV/FC** Gen9 heaps; execution gated | **IV/FC** Gen9/HAL200 heaps; execution gated | **IV/FC** descriptors + host DDI; no AGX3 execution | **IV/RE** Gen1 helpers; no traversal | **RE/FC** G16 SKSM/recovery/kick/ring; ACK unknown |
| G16C | **IV/FC** Gen9 heaps; execution gated | **IV/FC** Gen9/HAL200 heaps; execution gated | **IV/FC** descriptors + host DDI; no AGX3 execution | **IV/RE** Gen1 helpers; no traversal | **RE/FC** G16 SKSM/recovery/kick/ring; ACK unknown |
| G17P | **IV/FC** Gen9 heaps; execution gated | **IV/FC** Gen9/HAL200 heaps; execution gated | **IV/FC** descriptors + host DDI; no AGX3 execution | **IV/RE** Gen1 helpers; no traversal | **RE/FC** G16 SKSM/HAL200 power/kick/ring; ACK unknown |
| G17A | **IV/FC** Gen9 heaps; execution gated | **IV/FC** Gen9/HAL200 heaps; execution gated | **IV/FC** descriptors + host DDI; no AGX3 execution | **IV/RE** host contracts; no traversal | **RE/FC** dual-role/HAL only; submission unknown |
| G17G | **IV/FC** Gen10 heaps; execution gated | **IV/FC** Gen10/Gen5 heaps; execution gated | **IV/FC** descriptors + host DDI; no AGX3 execution | **IV/RE** Gen1+Gen5 BVH helpers; no traversal | **RE/FC** PI_300 G SKSM/power/boot/kick/ring; ACK unknown |
| G17S | **IV/FC** Gen10 heaps; execution gated | **IV/FC** Gen10/Gen5 heaps; execution gated | **IV/FC** descriptors/tile/host DDI; no AGX3 execution | **IV/RE** Gen1+Gen5 BVH helpers; no traversal | **RE/FC** PI_300 X SKSM/PM/recovery + loaded `a010`; UAT/start/ACK unknown |
| G17C | **IV/FC** Gen10 heaps; execution gated | **IV/FC** Gen10/Gen5 heaps; execution gated | **IV/FC** descriptors + host DDI; no AGX3 execution | **IV/RE** Gen1+Gen5 BVH helpers; no traversal | **RE/FC** PI_300 X SKSM/boot + paired `a000` offline; handoff/ACK unknown |
