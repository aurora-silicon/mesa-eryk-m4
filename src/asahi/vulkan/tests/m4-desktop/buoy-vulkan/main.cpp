// Vulkan windowed player for TekF's Buoy (shader/assets: CC BY-NC-SA 3.0).
// Fixed 3840x2160 linear-UNORM render target; SDL only supplies the
// window/surface.
#include <SDL.h>
#include <SDL_vulkan.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

static void check(VkResult r, const char *what) {
  if (r != VK_SUCCESS)
    throw std::runtime_error(std::string(what) + ": VkResult " +
                             std::to_string(r));
}
#define VK(call) check((call), #call)
static std::vector<char> read(const char *name) {
  std::ifstream f(name, std::ios::binary | std::ios::ate);
  if (!f)
    throw std::runtime_error(std::string("Cannot read ") + name);
  auto n = f.tellg();
  if (n <= 0)
    throw std::runtime_error("Empty asset");
  std::vector<char> b((size_t)n);
  f.seekg(0);
  f.read(b.data(), b.size());
  if (!f)
    throw std::runtime_error("Truncated asset");
  return b;
}
struct Image {
  VkImage image{};
  VkDeviceMemory memory{};
  VkImageView view{};
};
struct App {
  SDL_Window *window{};
  VkInstance instance{};
  VkSurfaceKHR surface{};
  VkPhysicalDevice physical{};
  VkDevice device{};
  VkQueue queue{};
  uint32_t family{};
  VkPhysicalDeviceMemoryProperties memory{};
  VkPhysicalDeviceProperties props{};
  VkCommandPool pool{};
  VkCommandBuffer cmd{};
  VkFence fence{};
  VkSemaphore acquired{};
  std::vector<VkSemaphore> ready;
  VkSwapchainKHR swap{};
  VkExtent2D extent{};
  std::vector<VkImage> swapImages;
  VkRenderPass pass{};
  VkFramebuffer fb{};
  VkPipelineLayout layout{};
  VkPipeline pipeline{};
  VkDescriptorSetLayout setLayout{};
  VkDescriptorPool descriptors{};
  VkDescriptorSet set{};
  VkSampler sampler{};
  Image noise, target;
  bool quit = false;
  char overlay[64] = "3840X2160  0.0 FPS";
  uint32_t memType(uint32_t bits, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < memory.memoryTypeCount; i++)
      if ((bits & (1u << i)) &&
          (memory.memoryTypes[i].propertyFlags & flags) == flags)
        return i;
    throw std::runtime_error("No suitable memory type");
  }
  Image image(uint32_t w, uint32_t h, VkImageUsageFlags usage) {
    Image out;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent = {w, h, 1};
    ci.mipLevels = ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    VK(vkCreateImage(device, &ci, nullptr, &out.image));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, out.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK(vkAllocateMemory(device, &ai, nullptr, &out.memory));
    VK(vkBindImageMemory(device, out.image, out.memory, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = out.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ci.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK(vkCreateImageView(device, &vi, nullptr, &out.view));
    return out;
  }
  void barrier(VkImage im, VkImageLayout old, VkImageLayout next,
               VkAccessFlags src, VkAccessFlags dst, VkPipelineStageFlags ss,
               VkPipelineStageFlags ds) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.oldLayout = old;
    b.newLayout = next;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = im;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, ss, ds, 0, 0, nullptr, 0, nullptr, 1, &b);
  }
  void drawOverlay() {
    // Five-column bitmap font, drawn with attachment clears; no extra shader.
    const char *alphabet = "0123456789.FPSMX ";
    const uint8_t glyph[][5] = {
        {62, 81, 73, 69, 62}, {0, 66, 127, 64, 0},   {66, 97, 81, 73, 70},
        {33, 65, 69, 75, 49}, {24, 20, 18, 127, 16}, {39, 69, 69, 69, 57},
        {60, 74, 73, 73, 48}, {1, 113, 9, 5, 3},     {54, 73, 73, 73, 54},
        {6, 73, 73, 41, 30},  {0, 96, 96, 0, 0},     {127, 9, 9, 9, 1},
        {127, 9, 9, 9, 6},    {38, 73, 73, 73, 50},  {127, 2, 12, 2, 127},
        {99, 20, 8, 20, 99},  {0, 0, 0, 0, 0}};
    VkClearAttachment a{};
    a.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    a.clearValue.color.float32[3] = 1;
    VkClearRect background{
        {{16, 16}, {(uint32_t)strlen(overlay) * 36 + 24, 66}}, 0, 1};
    vkCmdClearAttachments(cmd, 1, &a, 1, &background);
    std::vector<VkClearRect> pixels;
    for (unsigned i = 0; overlay[i]; i++) {
      const char *letter = strchr(alphabet, overlay[i]);
      if (!letter)
        continue;
      const auto &bits = glyph[letter - alphabet];
      for (int x = 0; x < 5; x++)
        for (int y = 0; y < 7; y++)
          if (bits[x] & (1u << y))
            pixels.push_back(
                {{{28 + (int)i * 36 + x * 6, 28 + y * 6}, {6, 6}}, 0, 1});
    }
    for (int i = 0; i < 4; i++)
      a.clearValue.color.float32[i] = 1;
    if (!pixels.empty())
      vkCmdClearAttachments(cmd, 1, &a, pixels.size(), pixels.data());
  }
  void capture(const char *path) {
    const size_t bytes = 3840 * 2160 * 4;
    VkBuffer buffer;
    VkDeviceMemory mem;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VK(vkCreateBuffer(device, &bi, nullptr, &buffer));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkAllocateMemory(device, &ai, nullptr, &mem));
    VK(vkBindBufferMemory(device, buffer, mem, 0));
    begin();
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {3840, 2160, 1};
    vkCmdCopyImageToBuffer(cmd, target.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                           &copy);
    VK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(queue));
    void *ptr;
    VK(vkMapMemory(device, mem, 0, bytes, 0, &ptr));
    std::ofstream out(path, std::ios::binary);
    out << "P6\n3840 2160\n255\n";
    for (size_t i = 0; i < bytes; i += 4)
      out.write((char *)ptr + i, 3);
    if (!out)
      throw std::runtime_error("Image capture write failed");
    vkUnmapMemory(device, mem);
    vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, mem, nullptr);
    printf("CAPTURE %s time=10 size=3840x2160\n", path);
  }
  void begin() {
    VK(vkResetCommandBuffer(cmd, 0));
    VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK(vkBeginCommandBuffer(cmd, &b));
  }
  void init() {
    if (SDL_Init(SDL_INIT_VIDEO))
      throw std::runtime_error(SDL_GetError());
    window = SDL_CreateWindow(
        "Buoy | Vulkan | compiling | 3840x2160", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 960, 540,
        SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!window)
      throw std::runtime_error(SDL_GetError());
    uint32_t n = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &n, nullptr))
      throw std::runtime_error(SDL_GetError());
    std::vector<const char *> ext(n);
    SDL_Vulkan_GetInstanceExtensions(window, &n, ext.data());
    uint32_t ne = 0;
    VK(vkEnumerateInstanceExtensionProperties(nullptr, &ne, nullptr));
    std::vector<VkExtensionProperties> available(ne);
    VK(vkEnumerateInstanceExtensionProperties(nullptr, &ne, available.data()));
    bool portability = false;
    for (auto &e : available)
      if (!strcmp(e.extensionName,
                  VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        portability = true;
    if (portability)
      ext.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Buoy Vulkan";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = ext.size();
    ci.ppEnabledExtensionNames = ext.data();
    ci.flags =
        portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0;
    VK(vkCreateInstance(&ci, nullptr, &instance));
    if (!SDL_Vulkan_CreateSurface(window, instance, &surface))
      throw std::runtime_error(SDL_GetError());
    VK(vkEnumeratePhysicalDevices(instance, &n, nullptr));
    std::vector<VkPhysicalDevice> devices(n);
    VK(vkEnumeratePhysicalDevices(instance, &n, devices.data()));
    for (auto d : devices) {
      uint32_t nq = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, nullptr);
      std::vector<VkQueueFamilyProperties> qp(nq);
      vkGetPhysicalDeviceQueueFamilyProperties(d, &nq, qp.data());
      for (uint32_t i = 0; i < nq; i++) {
        VkBool32 present = false;
        VK(vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present));
        if (present && (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
          physical = d;
          family = i;
          break;
        }
      }
      if (physical)
        break;
    }
    if (!physical)
      throw std::runtime_error("No graphics/present queue");
    vkGetPhysicalDeviceProperties(physical, &props);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    printf("DEVICE %s vendor=%x device=%x driver=%u\n", props.deviceName,
           props.vendorID, props.deviceID, props.driverVersion);
    std::vector<const char *> dext{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &n, nullptr));
    available.resize(n);
    VK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &n,
                                            available.data()));
    for (auto &e : available)
      if (!strcmp(e.extensionName, "VK_KHR_portability_subset"))
        dext.push_back("VK_KHR_portability_subset");
    float priority = 1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = dext.size();
    di.ppEnabledExtensionNames = dext.data();
    VK(vkCreateDevice(physical, &di, nullptr, &device));
    vkGetDeviceQueue(device, family, 0, &queue);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = family;
    VK(vkCreateCommandPool(device, &pi, nullptr, &pool));
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VK(vkAllocateCommandBuffers(device, &ca, &cmd));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK(vkCreateFence(device, &fi, nullptr, &fence));
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK(vkCreateSemaphore(device, &si, nullptr, &acquired));
    target = image(3840, 2160,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    noise = image(256, 256,
                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    upload();
    createPipeline();
    rebuildSwap();
  }
  void upload() {
    auto bytes = read("noise.rgba");
    if (bytes.size() != 256 * 256 * 4)
      throw std::runtime_error("Bad noise extent");
    VkBuffer b;
    VkDeviceMemory m;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = bytes.size();
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK(vkCreateBuffer(device, &bi, nullptr, &b));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, b, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex =
        memType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK(vkAllocateMemory(device, &ai, nullptr, &m));
    VK(vkBindBufferMemory(device, b, m, 0));
    void *p;
    VK(vkMapMemory(device, m, 0, bytes.size(), 0, &p));
    memcpy(p, bytes.data(), bytes.size());
    vkUnmapMemory(device, m);
    begin();
    barrier(noise.image, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {256, 256, 1};
    vkCmdCopyBufferToImage(cmd, b, noise.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(noise.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    VK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK(vkQueueWaitIdle(queue));
    vkDestroyBuffer(device, b, nullptr);
    vkFreeMemory(device, m, nullptr);
  }
  VkShaderModule shader(const char *name) {
    auto bytes = read(name);
    if (bytes.size() % 4)
      throw std::runtime_error("Invalid SPIR-V length");
    std::vector<uint32_t> words(bytes.size() / 4);
    memcpy(words.data(), bytes.data(), bytes.size());
    VkShaderModuleCreateInfo c{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    c.codeSize = bytes.size();
    c.pCode = words.data();
    VkShaderModule m;
    VK(vkCreateShaderModule(device, &c, nullptr, &m));
    return m;
  }
  void createPipeline() {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod = 0;
    VK(vkCreateSampler(device, &si, nullptr, &sampler));
    VkDescriptorSetLayoutBinding bind{0,
                                      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                      1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo sc{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sc.bindingCount = 1;
    sc.pBindings = &bind;
    VK(vkCreateDescriptorSetLayout(device, &sc, nullptr, &setLayout));
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pc{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pc.maxSets = 1;
    pc.poolSizeCount = 1;
    pc.pPoolSizes = &size;
    VK(vkCreateDescriptorPool(device, &pc, nullptr, &descriptors));
    VkDescriptorSetAllocateInfo da{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = descriptors;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &setLayout;
    VK(vkAllocateDescriptorSets(device, &da, &set));
    VkDescriptorImageInfo ii{sampler, noise.view,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = set;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4};
    VkPipelineLayoutCreateInfo li{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &setLayout;
    li.pushConstantRangeCount = 1;
    li.pPushConstantRanges = &push;
    VK(vkCreatePipelineLayout(device, &li, nullptr, &layout));
    VkAttachmentDescription a{};
    a.format = VK_FORMAT_R8G8B8A8_UNORM;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    a.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = 0;
    dep.dstSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &a;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    VK(vkCreateRenderPass(device, &rp, nullptr, &pass));
    VkFramebufferCreateInfo fc{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fc.renderPass = pass;
    fc.attachmentCount = 1;
    fc.pAttachments = &target.view;
    fc.width = 3840;
    fc.height = 2160;
    fc.layers = 1;
    VK(vkCreateFramebuffer(device, &fc, nullptr, &fb));
    VkShaderModule vs = shader("buoy.vert.spv"), fs = shader("buoy.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto &s : stages) {
      s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      s.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp{0, 0, 3840, 2160, 0, 1};
    VkRect2D rect{{0, 0}, {3840, 2160}};
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    viewport.pViewports = &vp;
    viewport.pScissors = &rect;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1;
    raster.cullMode = VK_CULL_MODE_NONE;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cb{};
    cb.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &cb;
    VkGraphicsPipelineCreateInfo gp{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &viewport;
    gp.pRasterizationState = &raster;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &blend;
    gp.layout = layout;
    gp.renderPass = pass;
    VK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr,
                                 &pipeline));
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
  }
  bool rebuildSwap() {
    int width, height;
    SDL_Vulkan_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0)
      return false;
    VK(vkDeviceWaitIdle(device));
    for (auto s : ready)
      vkDestroySemaphore(device, s, nullptr);
    ready.clear();
    if (swap)
      vkDestroySwapchainKHR(device, swap, nullptr);
    swap = VK_NULL_HANDLE;
    VkSurfaceCapabilitiesKHR caps;
    VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps));
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
      throw std::runtime_error("Surface cannot receive blits");
    uint32_t n;
    VK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(n);
    VK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n,
                                            formats.data()));
    VkSurfaceFormatKHR format{};
    bool found = false;
    for (auto f : formats)
      if ((f.format == VK_FORMAT_B8G8R8A8_UNORM ||
           f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
          f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        format = f;
        found = true;
        break;
      }
    if (!found)
      throw std::runtime_error(
          "No SDR UNORM surface format (avoid double gamma)");
    VkFormatProperties src, dst;
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM,
                                        &src);
    vkGetPhysicalDeviceFormatProperties(physical, format.format, &dst);
    if (!(src.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
        !(src.optimalTilingFeatures &
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ||
        !(dst.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
      throw std::runtime_error("Required blit support missing");
    extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
      extent.width = std::clamp((uint32_t)width, caps.minImageExtent.width,
                                caps.maxImageExtent.width);
      extent.height = std::clamp((uint32_t)height, caps.minImageExtent.height,
                                 caps.maxImageExtent.height);
    }
    uint32_t count = std::max(2u, caps.minImageCount);
    if (caps.maxImageCount)
      count = std::min(count, caps.maxImageCount);
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (unsigned b = 1; b <= 8; b <<= 1)
      if (caps.supportedCompositeAlpha & b) {
        alpha = (VkCompositeAlphaFlagBitsKHR)b;
        break;
      }
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface;
    ci.minImageCount = count;
    ci.imageFormat = format.format;
    ci.imageColorSpace = format.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = alpha;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    VK(vkCreateSwapchainKHR(device, &ci, nullptr, &swap));
    VK(vkGetSwapchainImagesKHR(device, swap, &n, nullptr));
    swapImages.resize(n);
    VK(vkGetSwapchainImagesKHR(device, swap, &n, swapImages.data()));
    ready.resize(n);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto &s : ready)
      VK(vkCreateSemaphore(device, &si, nullptr, &s));
    printf("SWAPCHAIN %ux%u | render=3840x2160 | FIFO | images=%u\n",
           extent.width, extent.height, n);
    return true;
  }
  bool draw(float time) {
    VK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    uint32_t index;
    VkResult ac = vkAcquireNextImageKHR(device, swap, 1000000000ull, acquired,
                                        VK_NULL_HANDLE, &index);
    if (ac == VK_ERROR_OUT_OF_DATE_KHR) {
      rebuildSwap();
      return false;
    }
    if (ac == VK_TIMEOUT || ac == VK_NOT_READY)
      return false;
    if (ac != VK_SUBOPTIMAL_KHR)
      check(ac, "acquire");
    begin();
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = pass;
    rb.framebuffer = fb;
    rb.renderArea = {{0, 0}, {3840, 2160}};
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                            &set, 0, nullptr);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &time);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    drawOverlay();
    vkCmdEndRenderPass(cmd);
    barrier(swapImages[index], VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkClearColorValue black{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, swapImages[index],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                         &range);
    barrier(swapImages[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    float scale = std::min(extent.width / 3840.f, extent.height / 2160.f);
    int w = (int)(3840 * scale), h = (int)(2160 * scale),
        x = (extent.width - w) / 2, y = (extent.height - h) / 2;
    VkImageBlit blit{};
    blit.srcSubresource =
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {3840, 2160, 1};
    blit.dstOffsets[0] = {x, y, 0};
    blit.dstOffsets[1] = {x + w, y + h, 1};
    vkCmdBlitImage(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapImages[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &blit, VK_FILTER_LINEAR);
    barrier(swapImages[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    VK(vkEndCommandBuffer(cmd));
    VK(vkResetFences(device, 1, &fence));
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquired;
    submit.pWaitDstStageMask = &wait;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &ready[index];
    VK(vkQueueSubmit(queue, 1, &submit, fence));
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &ready[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swap;
    present.pImageIndices = &index;
    VkResult pr = vkQueuePresentKHR(queue, &present);
    VK(vkWaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR ||
        ac == VK_SUBOPTIMAL_KHR)
      rebuildSwap();
    else
      check(pr, "present");
    return true;
  }
  void run(double seconds) {
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now(), last = start;
    unsigned frames = 0, total = 0;
    puts("READY: Buoy by TekF | Vulkan | fixed 3840x2160 | Escape quits");
    while (!quit) {
      double elapsed =
          std::chrono::duration<double>(Clock::now() - start).count();
      if (elapsed >= seconds)
        break;
      SDL_Event ev;
      while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT ||
            (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))
          quit = true;
      }
      if (quit)
        break;
      if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
        SDL_Delay(50);
        continue;
      }
      if (draw(total ? 10.f + (float)elapsed : 10.f)) {
        if (!total && getenv("BUOY_CAPTURE"))
          capture(getenv("BUOY_CAPTURE"));
        frames++;
        total++;
      }
      auto now = Clock::now();
      double dt = std::chrono::duration<double>(now - last).count();
      if (dt >= 1) {
        double fps = frames / dt;
        char title[1024];
        snprintf(title, sizeof(title),
                 "Buoy | 3840x2160 | %.1f FPS | %.2f ms/frame | %s", fps,
                 frames ? dt * 1000 / frames : 0, props.deviceName);
        SDL_SetWindowTitle(window, title);
        snprintf(overlay, sizeof(overlay), "3840X2160  %.1f FPS  %.1f MS", fps,
                 frames ? dt * 1000 / frames : 0);
        printf("FRAME_STATS elapsed=%.3f fps=%.3f ms=%.3f frames=%u\n", elapsed,
               fps, frames ? dt * 1000 / frames : 0, total);
        last = now;
        frames = 0;
      }
    }
    VK(vkDeviceWaitIdle(device));
    printf("DONE frames=%u seconds=%.3f\n", total,
           std::chrono::duration<double>(Clock::now() - start).count());
  }
  void cleanup() {
    if (device) {
      vkDeviceWaitIdle(device);
      vkDestroyPipeline(device, pipeline, nullptr);
      vkDestroyPipelineLayout(device, layout, nullptr);
      vkDestroyFramebuffer(device, fb, nullptr);
      vkDestroyRenderPass(device, pass, nullptr);
      vkDestroyDescriptorPool(device, descriptors, nullptr);
      vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
      vkDestroySampler(device, sampler, nullptr);
      for (auto i : {noise, target}) {
        vkDestroyImageView(device, i.view, nullptr);
        vkDestroyImage(device, i.image, nullptr);
        vkFreeMemory(device, i.memory, nullptr);
      }
      for (auto s : ready)
        vkDestroySemaphore(device, s, nullptr);
      vkDestroySwapchainKHR(device, swap, nullptr);
      vkDestroySemaphore(device, acquired, nullptr);
      vkDestroyFence(device, fence, nullptr);
      vkDestroyCommandPool(device, pool, nullptr);
      vkDestroyDevice(device, nullptr);
    }
    if (surface)
      vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance)
      vkDestroyInstance(instance, nullptr);
    if (window)
      SDL_DestroyWindow(window);
    SDL_Quit();
  }
};
int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  double seconds = 600;
  if (argc == 3 && !strcmp(argv[1], "--seconds"))
    seconds = std::stod(argv[2]);
  else if (argc != 1) {
    fprintf(stderr, "Usage: buoy-vulkan [--seconds 1..1200]\n");
    return 2;
  }
  if (!(seconds >= 1 && seconds <= 1200)) {
    fprintf(stderr, "Duration must be 1..1200 seconds\n");
    return 2;
  }
  App app;
  try {
    app.init();
    app.run(seconds);
    app.cleanup();
    return 0;
  } catch (const std::exception &e) {
    fprintf(stderr, "ERROR: %s\n", e.what());
    return 1;
  }
}
