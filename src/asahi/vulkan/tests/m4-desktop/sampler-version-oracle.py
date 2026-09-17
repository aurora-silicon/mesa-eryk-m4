#!/usr/bin/env python3
"""Build bounded fragment sampler-version oracles; this tool never runs the GPU.

Usage: sampler-version-oracle.py NEW_DIRECTORY
Copy the directory to the target and run its run.py under gpu-experiment.py.
The generated draws have 16 distinct explicit-LOD samples inside a loop, one
scalar combined sampler, exact constant mip colors, and distinct output channels.
They exercise ordinary, custom-border, reduction and view-minimum selection.
"""
import argparse,json,subprocess,shutil
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('output',type=Path)
p.add_argument('--workspace-root',type=Path,default=Path(__file__).resolve().parents[1])
a=p.parse_args()
root=a.workspace_root.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
s=(root/'mesa/m4-asahi/src/asahi/vulkan/tests/g17p_offscreen_triangle.c').read_text()
def replace(old,new):
 global s
 assert s.count(old)==1,old
 s=s.replace(old,new)
names=['vkCreateSampler','vkCreateDescriptorSetLayout','vkCreateDescriptorPool','vkAllocateDescriptorSets','vkUpdateDescriptorSets','vkCmdBindDescriptorSets','vkCmdClearColorImage','vkCmdPipelineBarrier']
replace('VK_FN(vkCreateDevice);','VK_FN(vkCreateDevice);\n'+'\n'.join('VK_FN('+n+');' for n in names))
replace('   LOAD_DEVICE(device, vkGetDeviceQueue);','   LOAD_DEVICE(device, vkGetDeviceQueue);\n'+'\n'.join('   LOAD_DEVICE(device, '+n+');' for n in names))
replace('.apiVersion = VK_API_VERSION_1_1,','.apiVersion = VK_API_VERSION_1_3,')
replace('   const VkDeviceCreateInfo device_info = {','''   const VkPhysicalDeviceImageViewMinLodFeaturesEXT min_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT,
      .minLod = VK_TRUE,
   };
   const VkPhysicalDeviceCustomBorderColorFeaturesEXT border_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT,
      .pNext = (void *)&min_features, .customBorderColors = VK_TRUE,
   };
   const VkPhysicalDeviceVulkan12Features sampler_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = (void *)&border_features, .samplerFilterMinmax = VK_TRUE,
   };
   const char *sampler_extensions[] = {VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME,
                                      VK_EXT_IMAGE_VIEW_MIN_LOD_EXTENSION_NAME};
   const VkDeviceCreateInfo device_info = {
      .pNext = &sampler_features,
      .enabledExtensionCount = 2, .ppEnabledExtensionNames = sampler_extensions,''')
replace('   const VkPipelineLayoutCreateInfo layout_info = {','''   const unsigned sampler_mode = getenv("M4_SAMPLER_CASE") ?
      strtoul(getenv("M4_SAMPLER_CASE"), NULL, 10) : 0;
   if (sampler_mode > 5) return 2;
   VkImage sampled_image;
   const VkImageCreateInfo sampled_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R32_SFLOAT, .extent = {4,4,1}, .mipLevels = 3,
      .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
   };
   check(vkCreateImage(device, &sampled_info, NULL, &sampled_image), "sample image");
   VkMemoryRequirements sampled_req;
   vkGetImageMemoryRequirements(device, sampled_image, &sampled_req);
   const VkMemoryAllocateInfo sampled_alloc = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = sampled_req.size,
      .memoryTypeIndex = find_memory_type(&memory_properties, sampled_req.memoryTypeBits,
                                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
   };
   VkDeviceMemory sampled_memory;
   check(vkAllocateMemory(device, &sampled_alloc, NULL, &sampled_memory), "sample memory");
   check(vkBindImageMemory(device, sampled_image, sampled_memory, 0), "sample bind");
   const VkImageViewMinLodCreateInfoEXT view_min = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_MIN_LOD_CREATE_INFO_EXT,
      .minLod = sampler_mode >= 4 ? 2.0f : 0.0f,
   };
   const VkImageViewCreateInfo sampled_view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .pNext = &view_min,
      .image = sampled_image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R32_SFLOAT,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,1},
   };
   VkImageView sampled_view;
   check(vkCreateImageView(device, &sampled_view_info, NULL, &sampled_view), "sample view");
   const bool custom_border = sampler_mode == 1 || sampler_mode == 5;
   const VkSamplerCustomBorderColorCreateInfoEXT border = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT,
      .customBorderColor.float32 = {.25f,0,0,1}, .format = VK_FORMAT_R32_SFLOAT,
   };
   const VkSamplerReductionModeCreateInfo reduction = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
      .pNext = custom_border ? &border : NULL,
      .reductionMode = sampler_mode == 2 ? VK_SAMPLER_REDUCTION_MODE_MIN :
                       sampler_mode == 3 ? VK_SAMPLER_REDUCTION_MODE_MAX :
                       VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE,
   };
   const VkSamplerCreateInfo sampler_info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = &reduction,
      .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU = custom_border ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV = custom_border ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER : VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT, .maxLod = 2,
      .borderColor = custom_border ? VK_BORDER_COLOR_FLOAT_CUSTOM_EXT : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
   };
   VkSampler sampler;
   check(vkCreateSampler(device, &sampler_info, NULL, &sampler), "sampler");
   const VkDescriptorSetLayoutBinding binding = {0,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                 1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
   const VkDescriptorSetLayoutCreateInfo set_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1, .pBindings = &binding,
   };
   VkDescriptorSetLayout set_layout;
   check(vkCreateDescriptorSetLayout(device, &set_info, NULL, &set_layout), "set layout");
   const VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
   const VkDescriptorPoolCreateInfo descriptor_pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
      .poolSizeCount = 1, .pPoolSizes = &pool_size,
   };
   VkDescriptorPool pool;
   check(vkCreateDescriptorPool(device, &descriptor_pool_info, NULL, &pool), "descriptor pool");
   const VkDescriptorSetAllocateInfo set_alloc = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool,
      .descriptorSetCount = 1, .pSetLayouts = &set_layout,
   };
   VkDescriptorSet set;
   check(vkAllocateDescriptorSets(device, &set_alloc, &set), "allocate set");
   const VkDescriptorImageInfo descriptor = {sampler,sampled_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
   const VkWriteDescriptorSet write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set,
      .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &descriptor,
   };
   vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
   const VkPipelineLayoutCreateInfo layout_info = {
      .setLayoutCount = 1, .pSetLayouts = &set_layout,''')
replace('   vkCmdBeginRenderPass(command_buffer, &render_begin,','''   VkImageMemoryBarrier sample_barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .image = sampled_image,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,1},
   };
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,0,NULL,0,NULL,1,&sample_barrier);
   for (unsigned level = 0; level < 3; ++level) {
      VkClearColorValue value = {.float32 = {level == 2 ? .5f : 1.0f,0,0,1}};
      VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT,level,1,0,1};
      vkCmdClearColorImage(command_buffer, sampled_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            &value,1,&range);
   }
   sample_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   sample_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   sample_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   sample_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
   vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,0,NULL,0,NULL,1,&sample_barrier);
   vkCmdBeginRenderPass(command_buffer, &render_begin,''')
replace('   vkCmdDraw(command_buffer, 3, 1, 0, 0);','''   vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout,0,1,&set,0,NULL);
   vkCmdDraw(command_buffer, 3, 1, 0, 0);''')
fixture=out/'fixture.c';fixture.write_text(s)
shutil.copy2(root/'mesa/m4-asahi/src/asahi/vulkan/tests/g17p_wsi_triangle_vert.h',out/'g17p_wsi_triangle_vert.h')
source=out/'sample.frag'
source.write_text('#version 450\nlayout(set=0,binding=0) uniform sampler2D tex;\nlayout(location=0) out vec4 color;\nvoid main(){float value=0.;for(int k=0;k<2+int(gl_FragCoord.x<0.);++k){\n'+''.join(f'value += textureLod(tex,vec2(2.5+{i}.0/512.+float(k)/1024.,2.5),0.).r;\n' for i in range(16))+'}color=(value/32.)*vec4(.25,.5,.75,1);}\n')
colors=[[64,128,191,255],[16,32,48,64],[64,128,191,255],[64,128,191,255],[32,64,96,128],[16,32,48,64]]
for mode,rgba in enumerate(colors):
 command=['python3',str(root/'tools/vulkan-fragment-probe.py'),str(source),'--output',str(out/str(mode)),'--fixture',str(fixture),'--rgba',*map(str,rgba)]
 with (out/f'{mode}-build.log').open('w') as log:subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT)
(out/'run.py').write_text('''import os,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True)
for mode in range(6):
 with (out/f'{mode}.log').open('w') as log:
  result=subprocess.run([str(root/str(mode)/'probe-arm64')],env=dict(os.environ,M4_SAMPLER_CASE=str(mode)),timeout=35,stdout=log,stderr=subprocess.STDOUT)
 print('SAMPLER_CASE',mode,'exit',result.returncode,flush=True)
 if result.returncode:raise SystemExit(result.returncode)
print('PASS: versioned fragment sampler oracles',flush=True)
''')
print(out)
