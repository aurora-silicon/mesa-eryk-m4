/*
 * Replicates dEQP-VK.draw.renderpass.multiple_interpolation single-varying
 * reference renders: a vertex buffer with interleaved vec4 position and vec4
 * colour, a 128x128 R8G8B8A8_UNORM colour target cleared by the render pass,
 * one perspective triangle, read back through vkCmdCopyImageToBuffer.
 *
 * Arguments name the pipelines to render in sequence (smooth, flat,
 * noperspective, centroid, sample). Each render prints the alpha histogram of
 * the covered pixels and a few sample rows. Build with
 *   clang --target=aarch64-linux-gnu ... -Ibuild/claude-fix/interpprobe
 *      tools/interp-sample-probe.c -o probe -lvulkan
 * On the M4 run only through gpu-experiment.py and run-m4-gl.py.
 */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vert_smooth.h"
#include "frag_smooth.h"
#include "vert_flat.h"
#include "frag_flat.h"
#include "vert_noperspective.h"
#include "frag_noperspective.h"
#include "vert_centroid.h"
#include "frag_centroid.h"
#include "vert_sample.h"
#include "frag_sample.h"
#include "vert_csample.h"
#include "vert_psample.h"
#include "vert_wsample.h"
#include "vert_multi.h"
#include "frag_multi.h"
#include "frag_wonly.h"
#include "frag_zonly.h"
#include "vert_extra.h"
#include "frag_extra.h"
#include "vert_extraflat.h"
#include "frag_extraflat.h"

#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s failed: %d\n", #x, r_); exit(1); } } while (0)
#define FMT (float_target ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM)
#define PX (float_target ? 16 : 4)
#define W 128
#define H 128

static VkDevice dev;
static int float_target; /* R32G32B32A32_SFLOAT target: print raw words */
static float colour_override[3][4] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}};
static VkPhysicalDeviceMemoryProperties memory;
static uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags flags) {
   for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
      if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
   fprintf(stderr, "no memory type\n"); exit(1);
}

struct shaders { const char *name; const uint32_t *vs; size_t vs_size; const uint32_t *fs; size_t fs_size; };
static const struct shaders table[] = {
   {"smooth", vert_smooth_spv, sizeof(vert_smooth_spv), frag_smooth_spv, sizeof(frag_smooth_spv)},
   {"flat", vert_flat_spv, sizeof(vert_flat_spv), frag_flat_spv, sizeof(frag_flat_spv)},
   {"noperspective", vert_noperspective_spv, sizeof(vert_noperspective_spv), frag_noperspective_spv, sizeof(frag_noperspective_spv)},
   {"centroid", vert_centroid_spv, sizeof(vert_centroid_spv), frag_centroid_spv, sizeof(frag_centroid_spv)},
   {"sample", vert_sample_spv, sizeof(vert_sample_spv), frag_sample_spv, sizeof(frag_sample_spv)},
   /* variants: constant colours, constant positions, attribute colour with constant alpha */
   {"csample", vert_csample_spv, sizeof(vert_csample_spv), frag_sample_spv, sizeof(frag_sample_spv)},
   {"psample", vert_psample_spv, sizeof(vert_psample_spv), frag_sample_spv, sizeof(frag_sample_spv)},
   {"wsample", vert_wsample_spv, sizeof(vert_wsample_spv), frag_sample_spv, sizeof(frag_sample_spv)},
   {"wonly", vert_sample_spv, sizeof(vert_sample_spv), frag_wonly_spv, sizeof(frag_wonly_spv)},
   {"zonly", vert_sample_spv, sizeof(vert_sample_spv), frag_zonly_spv, sizeof(frag_zonly_spv)},
   {"extra", vert_extra_spv, sizeof(vert_extra_spv), frag_extra_spv, sizeof(frag_extra_spv)},
   {"extraflat", vert_extraflat_spv, sizeof(vert_extraflat_spv), frag_extraflat_spv, sizeof(frag_extraflat_spv)},
   /* the CTS five-varying shader pair; the push constant selects the input (multi0..multi4) */
   {"multi0", vert_multi_spv, sizeof(vert_multi_spv), frag_multi_spv, sizeof(frag_multi_spv)},
   {"multi1", vert_multi_spv, sizeof(vert_multi_spv), frag_multi_spv, sizeof(frag_multi_spv)},
   {"multi2", vert_multi_spv, sizeof(vert_multi_spv), frag_multi_spv, sizeof(frag_multi_spv)},
   {"multi3", vert_multi_spv, sizeof(vert_multi_spv), frag_multi_spv, sizeof(frag_multi_spv)},
   {"multi4", vert_multi_spv, sizeof(vert_multi_spv), frag_multi_spv, sizeof(frag_multi_spv)},
};

static VkShaderModule module(const uint32_t *code, size_t size) {
   VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code};
   VkShaderModule m; CHECK(vkCreateShaderModule(dev, &info, NULL, &m)); return m;
}

static void render(VkQueue queue, unsigned family, const struct shaders *sh, int sample_shading, uint32_t pc, uint8_t *out) {
   VkShaderModule vs = module(sh->vs, sh->vs_size), fs = module(sh->fs, sh->fs_size);
   float verts[3][8] = {
      {-1.5f, -0.4f, 1.0f, 2.0f, 1, 0, 0, 1},
      {0.4f, -0.4f, 0.5f, 0.5f, 0, 1, 0, 1},
      {0.3f, 0.8f, 0.0f, 1.0f, 0, 0, 1, 1},
   };
   for (unsigned v = 0; v < 3; ++v) for (unsigned c = 0; c < 4; ++c)
      if (colour_override[v][c] >= 0) verts[v][4 + c] = colour_override[v][c];
   VkBuffer vbuf; VkDeviceMemory vmem; VkMemoryRequirements req;
   VkBufferCreateInfo vbi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(verts), .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
   CHECK(vkCreateBuffer(dev, &vbi, NULL, &vbuf)); vkGetBufferMemoryRequirements(dev, vbuf, &req);
   VkMemoryAllocateInfo vai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
   CHECK(vkAllocateMemory(dev, &vai, NULL, &vmem)); CHECK(vkBindBufferMemory(dev, vbuf, vmem, 0));
   void *vp; CHECK(vkMapMemory(dev, vmem, 0, VK_WHOLE_SIZE, 0, &vp)); memcpy(vp, verts, sizeof(verts)); vkUnmapMemory(dev, vmem);

   VkImage image; VkDeviceMemory imem;
   VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = FMT,
      .extent = {W, H, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
   CHECK(vkCreateImage(dev, &ii, NULL, &image)); vkGetImageMemoryRequirements(dev, image, &req);
   VkMemoryAllocateInfo iai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
   CHECK(vkAllocateMemory(dev, &iai, NULL, &imem)); CHECK(vkBindImageMemory(dev, image, imem, 0));
   VkImageView view; VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = FMT, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
   CHECK(vkCreateImageView(dev, &vi, NULL, &view));

   VkBuffer rbuf; VkDeviceMemory rmem;
   VkBufferCreateInfo rbi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = W * H * PX, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
   CHECK(vkCreateBuffer(dev, &rbi, NULL, &rbuf)); vkGetBufferMemoryRequirements(dev, rbuf, &req);
   VkMemoryAllocateInfo rai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = memtype(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
   CHECK(vkAllocateMemory(dev, &rai, NULL, &rmem)); CHECK(vkBindBufferMemory(dev, rbuf, rmem, 0));

   VkAttachmentDescription ad = {.format = FMT, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
   VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
   VkSubpassDescription sub = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &ref};
   VkRenderPassCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &ad, .subpassCount = 1, .pSubpasses = &sub};
   VkRenderPass pass; CHECK(vkCreateRenderPass(dev, &ri, NULL, &pass));
   VkFramebufferCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = pass, .attachmentCount = 1, .pAttachments = &view, .width = W, .height = H, .layers = 1};
   VkFramebuffer fb; CHECK(vkCreateFramebuffer(dev, &fi, NULL, &fb));

   VkPushConstantRange pcr = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4};
   VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr};
   VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(dev, &pli, NULL, &layout));
   VkPipelineShaderStageCreateInfo stages[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"}};
   VkVertexInputBindingDescription bind = {0, 32, VK_VERTEX_INPUT_RATE_VERTEX};
   VkVertexInputAttributeDescription attrs[2] = {{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16}};
   VkPipelineVertexInputStateCreateInfo vin = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &bind, .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attrs};
   VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
   VkViewport viewport = {0, 0, W, H, 0, 1}; VkRect2D scissor = {{0, 0}, {W, H}};
   VkPipelineViewportStateCreateInfo vp_state = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
   VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1};
   VkPipelineMultisampleStateCreateInfo msaa = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT, .sampleShadingEnable = sample_shading, .minSampleShading = 1.0f};
   VkPipelineColorBlendAttachmentState blend_att = {.colorWriteMask = 0xf};
   VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend_att};
   VkPipelineDepthStencilStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
   VkGraphicsPipelineCreateInfo gp = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .stageCount = 2, .pStages = stages, .pVertexInputState = &vin, .pInputAssemblyState = &ia,
      .pViewportState = &vp_state, .pRasterizationState = &rs, .pMultisampleState = &msaa, .pDepthStencilState = &ds, .pColorBlendState = &blend, .layout = layout, .renderPass = pass};
   VkPipeline pipeline; CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, NULL, &pipeline));

   VkCommandPool pool; VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
   CHECK(vkCreateCommandPool(dev, &pi, NULL, &pool));
   VkCommandBuffer cmd; VkCommandBufferAllocateInfo ca = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
   CHECK(vkAllocateCommandBuffers(dev, &ca, &cmd));
   VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(cmd, &begin));
   VkClearValue clear = {.color.float32 = {0, 0, 0, 1}};
   VkRenderPassBeginInfo rb = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = pass, .framebuffer = fb, .renderArea = {{0, 0}, {W, H}}, .clearValueCount = 1, .pClearValues = &clear};
   vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
   vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   VkDeviceSize zero = 0; vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &zero);
   vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &pc);
   vkCmdDraw(cmd, 3, 1, 0, 0);
   vkCmdEndRenderPass(cmd);
   VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
   VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {W, H, 1}};
   vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rbuf, 1, &copy);
   VkMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0, NULL);
   CHECK(vkEndCommandBuffer(cmd));
   VkFence fence; VkFenceCreateInfo fc = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; CHECK(vkCreateFence(dev, &fc, NULL, &fence));
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
   CHECK(vkQueueSubmit(queue, 1, &si, fence)); CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull));
   void *rp; CHECK(vkMapMemory(dev, rmem, 0, VK_WHOLE_SIZE, 0, &rp)); memcpy(out, rp, W * H * PX); vkUnmapMemory(dev, rmem);

   vkDestroyFence(dev, fence, NULL); vkDestroyCommandPool(dev, pool, NULL); vkDestroyPipeline(dev, pipeline, NULL); vkDestroyPipelineLayout(dev, layout, NULL);
   vkDestroyFramebuffer(dev, fb, NULL); vkDestroyRenderPass(dev, pass, NULL); vkDestroyImageView(dev, view, NULL); vkDestroyImage(dev, image, NULL); vkFreeMemory(dev, imem, NULL);
   vkDestroyBuffer(dev, rbuf, NULL); vkFreeMemory(dev, rmem, NULL); vkDestroyBuffer(dev, vbuf, NULL); vkFreeMemory(dev, vmem, NULL);
   vkDestroyShaderModule(dev, vs, NULL); vkDestroyShaderModule(dev, fs, NULL);
}

int main(int argc, char **argv) {
   VkInstance instance; VkInstanceCreateInfo ic = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
   CHECK(vkCreateInstance(&ic, NULL, &instance));
   uint32_t n = 1; VkPhysicalDevice physical; CHECK(vkEnumeratePhysicalDevices(instance, &n, &physical));
   VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical, &properties);
   printf("Device: %s\n", properties.deviceName);
   vkGetPhysicalDeviceMemoryProperties(physical, &memory);
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, NULL);
   VkQueueFamilyProperties *families = calloc(n, sizeof(*families));
   vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families);
   unsigned family = 0; while (family < n && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) family++;
   if (family == n) return 1;
   free(families);
   float priority = 1;
   VkPhysicalDeviceFeatures features = {.sampleRateShading = VK_TRUE};
   VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
   VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi, .pEnabledFeatures = &features};
   CHECK(vkCreateDevice(physical, &di, NULL, &dev)); VkQueue queue; vkGetDeviceQueue(dev, family, 0, &queue);

   static uint8_t out[W * H * 16], first[W * H * 16];
   int bad = 0;
   for (int a = 1; a < argc; ++a) {
      const char *name = argv[a]; int srs = 0;
      if (!strcmp(name, "F")) { float_target = 1; continue; }
      if (name[0] == 'A' && name[1] == ':') { /* A:a0,a1,a2 sets the vertex alphas */
         sscanf(name + 2, "%f,%f,%f", &colour_override[0][3], &colour_override[1][3], &colour_override[2][3]); continue; }
      if (name[0] == 'K' && name[1] == ':') { /* K:r,g,b,a gives every vertex the same colour */
         float k[4]; sscanf(name + 2, "%f,%f,%f,%f", &k[0], &k[1], &k[2], &k[3]);
         for (unsigned v = 0; v < 3; ++v) for (unsigned c = 0; c < 4; ++c) colour_override[v][c] = k[c]; continue; }
      if (name[0] == '+') { srs = 1; name++; }
      const struct shaders *sh = NULL;
      for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); ++i) if (!strcmp(table[i].name, name)) sh = &table[i];
      if (!sh) { fprintf(stderr, "unknown pipeline %s\n", name); return 2; }
      uint32_t pc = strncmp(name, "multi", 5) ? 0 : (uint32_t)(name[5] - '0');
      render(queue, family, sh, srs, pc, out);
      if (float_target) {
         printf("%s raw:", name);
         for (unsigned i = 0; i < 4; ++i) {
            static const unsigned px[4][2] = {{32, 64}, {48, 64}, {72, 30}, {100, 100}};
            uint32_t w[4]; memcpy(w, out + (px[i][1] * W + px[i][0]) * 16, 16);
            printf(" (%u,%u)=%08x,%08x,%08x,%08x", px[i][0], px[i][1], w[0], w[1], w[2], w[3]);
         }
         printf("\n"); continue;
      }
      unsigned covered = 0, low = 0, minalpha = 255, rgbdiff = 0;
      for (unsigned p = 0; p < W * H; ++p) {
         const uint8_t *px = out + p * 4;
         if (px[0] == 0 && px[1] == 0 && px[2] == 0 && px[3] == 255) continue;
         covered++;
         if (px[3] != 255) { low++; if (px[3] < minalpha) minalpha = px[3]; }
      }
      if (a == 1) memcpy(first, out, sizeof(first));
      else for (unsigned p = 0; p < W * H; ++p) if (memcmp(first + p * 4, out + p * 4, 3)) rgbdiff++;
      printf("%s%s: covered=%u alpha<255=%u min_alpha=%u rgb_diff_vs_first=%u row64:", srs ? "+" : "", name, covered, low, minalpha, rgbdiff);
      for (unsigned x = 32; x < 96; x += 16) printf(" %u,%u,%u,%u", out[(64 * W + x) * 4], out[(64 * W + x) * 4 + 1], out[(64 * W + x) * 4 + 2], out[(64 * W + x) * 4 + 3]);
      printf(" row30:");
      for (unsigned x = 72; x < 104; x += 16) printf(" %u,%u,%u,%u", out[(30 * W + x) * 4], out[(30 * W + x) * 4 + 1], out[(30 * W + x) * 4 + 2], out[(30 * W + x) * 4 + 3]);
      printf("\n");
      if (low && strcmp(name, "flat") && strcmp(name, "noperspective")) bad++;
   }
   printf("INTERP_SAMPLE_RESULT: %s\n", bad ? "FAIL" : "PASS");
   vkDestroyDevice(dev, NULL); vkDestroyInstance(instance, NULL);
   return bad ? 1 : 0;
}
