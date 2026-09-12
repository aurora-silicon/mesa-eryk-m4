/* SPDX-License-Identifier: MIT
 * Fragment-probe generator snippet. PROBE_* are replaced before compilation.
 */
   VkBuffer storage_buffer;
   VkBufferCreateInfo storage_bi = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = PROBE_WORD_COUNT * 4 + 64,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
   };
   check(vkCreateBuffer(device, &storage_bi, NULL, &storage_buffer), "storage buffer");
   VkMemoryRequirements storage_req;
   vkGetBufferMemoryRequirements(device, storage_buffer, &storage_req);
   VkMemoryAllocateInfo storage_ai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = storage_req.size,
      .memoryTypeIndex = find_memory_type(&memory_properties, storage_req.memoryTypeBits,
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
   };
   VkDeviceMemory storage_memory;
   check(vkAllocateMemory(device, &storage_ai, NULL, &storage_memory), "storage memory");
   check(vkBindBufferMemory(device, storage_buffer, storage_memory, 0), "bind storage");
   uint32_t *storage_map;
   check(vkMapMemory(device, storage_memory, 0, storage_bi.size, 0, (void **)&storage_map), "map storage");
   memset(storage_map, 0, PROBE_WORD_COUNT * 4);
   memset(storage_map + PROBE_WORD_COUNT, 0xcd, 64);
   VkDescriptorSetLayoutBinding storage_binding = {
      .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1, .stageFlags = PROBE_STAGE,
   };
   VkDescriptorSetLayoutCreateInfo storage_li = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1, .pBindings = &storage_binding,
   };
   VkDescriptorSetLayout storage_layout;
   check(vkCreateDescriptorSetLayout(device, &storage_li, NULL, &storage_layout), "storage layout");
   VkDescriptorPoolSize storage_ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
   VkDescriptorPoolCreateInfo storage_pi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &storage_ps,
   };
   VkDescriptorPool storage_pool;
   check(vkCreateDescriptorPool(device, &storage_pi, NULL, &storage_pool), "storage pool");
   VkDescriptorSetAllocateInfo storage_si = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = storage_pool, .descriptorSetCount = 1,
      .pSetLayouts = &storage_layout,
   };
   VkDescriptorSet storage_set;
   check(vkAllocateDescriptorSets(device, &storage_si, &storage_set), "storage set");
   VkDescriptorBufferInfo storage_desc = {storage_buffer, 0, PROBE_WORD_COUNT * 4};
   VkWriteDescriptorSet storage_write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = storage_set, .dstBinding = 0, .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &storage_desc,
   };
   vkUpdateDescriptorSets(device, 1, &storage_write, 0, NULL);
