# Copyright © Microsoft Corporation
# SPDX-License-Identifier: MIT

# d3d12agx builds the NIR->DXIL compiler to GENERATE probe shaders. `dxc`
# does not run on Asahi/aarch64, so without this the only DXIL available
# for tests is the LGPL vkd3d-proton corpus, which cannot be vendored into
# this MIT tree. This is a build-time tool only: no application shader
# ever passes through it, and nothing in libd3d12agx links it.
if with_microsoft_clc or with_spirv_to_dxil or with_microsoft_vk or get_option('asahi-d3d12') or (with_gallium_d3d12 and not get_option('gallium-d3d12-graphics').disabled())
  subdir('compiler')
  if with_microsoft_clc
    subdir('clc')
  endif
  if with_spirv_to_dxil or with_microsoft_vk
    subdir('spirv_to_dxil')
  endif
  if with_microsoft_vk
    subdir('vulkan')
  endif
endif