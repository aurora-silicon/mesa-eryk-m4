/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_buffer_view.h"
#include "asahi/layout/layout.h"
#include "asahi/lib/agx_nir_lower_vbo.h"
#include "util/bitscan.h"
#include "util/format/u_format.h"
#include "util/format/u_formats.h"

#include "agx_helpers.h"
#include "agx_nir_texture.h"
#include "agx_pack.h"
#include "hk_buffer.h"
#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_image.h"
#include "hk_physical_device.h"

VkFormatFeatureFlags2
hk_get_buffer_format_features(struct hk_physical_device *pdev,
                              VkFormat vk_format)
{
   VkFormatFeatureFlags2 features = 0;
   enum pipe_format p_format = hk_format_to_pipe_format(vk_format);

   if (p_format == PIPE_FORMAT_NONE)
      return 0;

   if (pdev->dev.chip == AGX_CHIP_G16G &&
       hk_storage_image_format(p_format) != p_format)
      return VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
             VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;

   if (agx_vbo_supports_format(p_format))
      features |= VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT;

   if (ail_pixel_format[p_format].texturable &&
       !util_format_is_depth_or_stencil(p_format)) {

      features |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT;

      /* RGB32 specially supported for uniform texel buffers only. */
      if (util_is_power_of_two_nonzero(util_format_get_blocksize(p_format))) {
         /* Reads obtain their format from the buffer texture descriptor. */
         features |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
                     VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
                     VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
      }

      if (p_format == PIPE_FORMAT_R32_UINT || p_format == PIPE_FORMAT_R32_SINT)
         features |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;

      /* The Gen9 unformatted-store lowering converts 8/16-bit and sRGB
       * channels; alpha-only and packed 10-bit signed buffers are not
       * covered and the image path does not advertise them either
       * (image.store.without_format.buffer.{a8_unorm,a2b10g10r10_sint,
       * a2r10g10b10_sint}*). Follow the image advertisement. */
      if (agx_chip_descriptor_generation(pdev->dev.chip) == 9 &&
          !(hk_get_image_format_features(pdev, vk_format,
                                         VK_IMAGE_TILING_OPTIMAL) &
            VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT))
         features &= ~(VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
                       VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT);
   }

   return features;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CreateBufferView(VkDevice _device, const VkBufferViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkBufferView *pBufferView)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   VK_FROM_HANDLE(hk_buffer, buffer, pCreateInfo->buffer);
   struct hk_buffer_view *view;

   view = vk_buffer_view_create(&device->vk, pCreateInfo, pAllocator,
                                sizeof(*view));
   if (!view)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   enum pipe_format format = hk_format_to_pipe_format(view->vk.format);
   if (device->dev.chip == AGX_CHIP_G16G)
      format = hk_storage_image_format(format);
   const struct util_format_description *desc = util_format_description(format);

   uint8_t format_swizzle[4] = {
      desc->swizzle[0],
      desc->swizzle[1],
      desc->swizzle[2],
      desc->swizzle[3],
   };

   if (util_format_is_depth_or_stencil(format)) {
      assert(!util_format_is_depth_and_stencil(format) &&
             "separate stencil always used");

      /* Broadcast depth and stencil */
      format_swizzle[0] = 0;
      format_swizzle[1] = 0;
      format_swizzle[2] = 0;
      format_swizzle[3] = 0;
   }

   /* Decompose the offset into a multiple of 16-bytes (which we can include in
    * the address) and an extra texel-aligned tail offset of up to 15 bytes.
    *
    * This lets us offset partially in the shader instead, getting
    * around alignment restrictions on the base address pointer.
    */
   uint64_t head_offset_B = view->vk.offset & ~0xf;
   uint64_t rw_base = hk_buffer_address_rw(buffer, 0) + head_offset_B;
   uint64_t ro_base = hk_buffer_address_ro(buffer, 0) + head_offset_B;
   const unsigned generation =
      agx_chip_descriptor_generation(device->dev.chip);
   const unsigned blocksize_B = util_format_get_blocksize(format);
   const bool rgb32 = generation == 9 && blocksize_B == 12;
   view->residency = (struct hk_texel_buffer_residency) {
      .map = buffer->residency ? buffer->residency->va->addr +
                                  (view->vk.offset / 16384) * sizeof(uint32_t) : 0,
      .page_offset_B = view->vk.offset % 16384,
      .texel_size_B = blocksize_B,
      .elements = view->vk.elements,
   };
   uint32_t tail_offset_B = view->vk.offset & 0xf;
   uint32_t tail_offset_el = tail_offset_B / blocksize_B;
   assert((rgb32 ? tail_offset_B % 4 == 0
                 : tail_offset_el * blocksize_B == tail_offset_B) &&
          "must be texel (or RGB32 component) aligned");

   /* Include the leading texels when a shifted view crosses a hardware row.
    * Gen9 RGB32 uses raw loads: its software offset is in bytes so offsets
    * aligned to a single 32-bit component are representable as Vulkan allows.
    */
   const uint32_t span_el = view->vk.elements +
      DIV_ROUND_UP(tail_offset_B, blocksize_B);

   struct AGX_TEXTURE texture = {AGX_TEXTURE_header};
   {
      texture.dimension = AGX_TEXTURE_DIMENSION_2D;
      texture.layout = AGX_LAYOUT_LINEAR;
      texture.channels = ail_pixel_format[format].channels;
      texture.type = ail_pixel_format[format].type;
      texture.swizzle_r = agx_channel_from_pipe(format_swizzle[0]);
      texture.swizzle_g = agx_channel_from_pipe(format_swizzle[1]);
      texture.swizzle_b = agx_channel_from_pipe(format_swizzle[2]);
      texture.swizzle_a = agx_channel_from_pipe(format_swizzle[3]);

      texture.width = AGX_TEXTURE_BUFFER_WIDTH;
      texture.height = DIV_ROUND_UP(span_el, texture.width);
      texture.first_level = texture.last_level = 0;

      texture.address = ro_base;
      texture.buffer_size_sw = view->vk.elements;
      texture.buffer_offset_sw = rgb32 ? tail_offset_B : tail_offset_el;

      texture.srgb = (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
      texture.srgb_2_channel =
         texture.srgb && util_format_colormask(desc) == 0x3;

      texture.depth = 1;
      texture.stride =
         (texture.width * util_format_get_blocksize(format)) - 16;
   }
   if (!hk_pack_native_texture(generation, &view->tex, &texture)) {
      vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
      return vk_error(device, VK_ERROR_FORMAT_NOT_SUPPORTED);
   }

   if (hk_native_pbe_hardware_supported(generation)) {
      struct AGX_PBE cfg = {AGX_PBE_header};
      cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
      cfg.layout = AGX_LAYOUT_LINEAR;
      cfg.channels = ail_pixel_format[format].channels;
      cfg.type = ail_pixel_format[format].type;
      cfg.srgb = util_format_is_srgb(format);

      assert(desc->nr_channels >= 1 && desc->nr_channels <= 4);

      for (unsigned i = 0; i < desc->nr_channels; ++i) {
         if (desc->swizzle[i] == 0)
            cfg.swizzle_r = i;
         else if (desc->swizzle[i] == 1)
            cfg.swizzle_g = i;
         else if (desc->swizzle[i] == 2)
            cfg.swizzle_b = i;
         else if (desc->swizzle[i] == 3)
            cfg.swizzle_a = i;
      }

      cfg.buffer = rw_base;
      cfg.buffer_offset_sw = tail_offset_el;

      cfg.width = AGX_TEXTURE_BUFFER_WIDTH;
      cfg.height = DIV_ROUND_UP(span_el, cfg.width);
      cfg.level = 0;
      cfg.stride = (cfg.width * util_format_get_blocksize(format)) - 4;
      cfg.layers = 1;
      cfg.levels = 1;

      /* Gen9 storage stores go through the unformatted-store lowering,
       * which reads type/channels/sRGB from a software-ABI copy and
       * encodes sRGB itself (as image views do). Give the shader that copy
       * and keep the native heap entry linear so the PBE does not encode
       * a second time (image.store.without_format.buffer.*_srgb, a8_unorm,
       * a2b10g10r10_sint_pack32). */
      if (generation == 9) {
         memset(&view->pbe_software, 0, sizeof(view->pbe_software));
         AGX_PBE_pack(view->pbe_software.opaque, &cfg);
         view->storage_software_valid = true;
         cfg.srgb = false;
         /* Descriptor crawls apply the tail from the software PBE before
          * converting coordinates to 2D. The hardware entry only needs the
          * aligned base, and has no software tail field.
          */
         cfg.buffer_offset_sw = 0;
      }
      view->storage_pbe_valid =
         hk_pack_native_pbe(generation, &view->pbe, &cfg);
   } else {
      memset(&view->pbe, 0, sizeof(view->pbe));
   }

   if (agx_native_graphics(&device->dev)) {
      VkResult result = hk_descriptor_table_add(
         device, &device->sampled_images, &view->tex,
         hk_texture_descriptor_size(generation), &view->sampled_heap_index);
      if (result != VK_SUCCESS) {
         vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
         return result;
      }
   }

   /* Native image stores address the PBE through the shared descriptor heap,
    * including buffer images lowered to a linear 2D view. */
   if (agx_native_graphics(&device->dev) && view->storage_pbe_valid &&
       (buffer->vk.usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) {
      VkResult result = hk_descriptor_table_add(
         device, &device->sampled_images, &view->pbe,
         hk_pbe_descriptor_size(generation), &view->storage_heap_index);
      if (result != VK_SUCCESS) {
         hk_descriptor_table_remove(device, &device->sampled_images,
                                    view->sampled_heap_index);
         vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
         return result;
      }
   }

   *pBufferView = hk_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
hk_DestroyBufferView(VkDevice _device, VkBufferView bufferView,
                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   VK_FROM_HANDLE(hk_buffer_view, view, bufferView);

   if (!view)
      return;

   if (view->storage_heap_index)
      hk_descriptor_table_remove(device, &device->sampled_images,
                                 view->storage_heap_index);

   if (view->sampled_heap_index)
      hk_descriptor_table_remove(device, &device->sampled_images,
                                 view->sampled_heap_index);

   vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
}
