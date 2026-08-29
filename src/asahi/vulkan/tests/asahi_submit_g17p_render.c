/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Direct Linux UAPI proof for the smallest G17P render: a terminate-only VDM
 * stream, a constant RT0 background program, and one end-of-tile PBE store.
 * No Vulkan or display driver is involved.
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "drm-uapi/asahi_drm.h"

#define GPU_PAGE_SIZE UINT64_C(0x4000)
#define USC_BASE UINT64_C(0x10000000000)
#define CODE_PAGE_VA UINT64_C(0x10000000000)
#define VDM_VA UINT64_C(0x10000018000)
#define PBE_PAGE_VA UINT64_C(0x10000020000)
#define PBE_ARGUMENT_VA UINT64_C(0x10000020220)
#define TARGET_VA UINT64_C(0x10000088000)
#define SCISSOR_VA UINT64_C(0x10000098000)
#define PIPELINE_PAGE_VA UINT64_C(0x10001990000)
#define BG_PROGRAM_VA UINT64_C(0x10001990240)
#define EOT_PROGRAM_VA UINT64_C(0x10001990640)
#define KERNEL_START UINT64_C(0x7000000000)
#define KERNEL_END UINT64_C(0x7100000000)
#define TARGET_WIDTH 64u
#define TARGET_HEIGHT 64u
#define TARGET_BYTES (TARGET_WIDTH * TARGET_HEIGHT * 4u)

#define BG_RESOURCE_SPEC UINT64_C(0x0007800000000040)
#define EOT_RESOURCE_SPEC UINT64_C(0x8)

/* AGX3b flat fragment shader: RT0 = (0.25, 0.5, 0.75, 1.0). */
static const uint8_t flat_fragment_shader[] = {
   0x3c, 0x80, 0x02, 0x3e, 0x00, 0x00, 0x00, 0x0c, 0x0c,
   0x80, 0x02, 0x3e, 0x00, 0x00, 0x00, 0x04, 0x1c, 0x80,
   0x02, 0x3e, 0x00, 0x00, 0x00, 0x08, 0x2c, 0x80, 0x02,
   0x3e, 0x00, 0x00, 0x00, 0x0a, 0x87, 0x02, 0x54, 0x0c,
   0x08, 0x00, 0xe7, 0x06, 0x54, 0x00, 0x00, 0x00, 0x01,
   0x4e, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
};

/* 128-byte EOT compiler text; main begins at +0x40. */
static const uint8_t eot_shader_text[] = {
   0x0e, 0x00, 0x00, 0x00, 0x06, 0x00, 0x06, 0x00,
   0x06, 0x00, 0x06, 0x00, 0x06, 0x00, 0x06, 0x00,
   0x06, 0x00, 0x06, 0x00, 0x06, 0x00, 0x06, 0x00,
   0x06, 0x00, 0x06, 0x00, 0x06, 0x00, 0x06, 0x00,
   0x06, 0x00, 0x06, 0x00, 0x06, 0x00, 0x06, 0x00,
   0x06, 0x00, 0x06, 0x00, 0x06, 0x00, 0x06, 0x00,
   0x06, 0x00, 0x06, 0x00, 0x06, 0x00, 0x06, 0x00,
   0x06, 0x00, 0x06, 0x00, 0x06, 0x00, 0x06, 0x00,
   0x3c, 0xa0, 0x10, 0x06, 0x2c, 0xa1, 0x10, 0x26,
   0x0b, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00,
   0x97, 0x05, 0x54, 0x04, 0x00, 0x00, 0x00, 0x00,
   0x50, 0x50, 0x10, 0x00, 0x0b, 0x07, 0x2f, 0x00,
   0x42, 0x20, 0x00, 0x08, 0x00, 0x00, 0x1b, 0x05,
   0x2f, 0x00, 0x42, 0x40, 0x00, 0x08, 0x00, 0x00,
   0x57, 0x40, 0x54, 0x00, 0x00, 0x00, 0x00, 0x80,
   0xaa, 0x75, 0x02, 0x00, 0x0e, 0x00, 0x00, 0x00,
};

/* No-resource background Ioto program for flat_fragment_shader at +0x80. */
static const uint8_t bg_program[] = {
   0x77, 0x00, 0x2a, 0x43, 0x4b, 0x00, 0x00, 0x00, 0x77,
   0x01, 0x2a, 0x01, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00,
   0xf7, 0x00, 0x2a, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x1c, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x14, 0x81, 0x11, 0x06, 0x00, 0x00, 0x00, 0x00, 0x0c,
   0x80, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x9f, 0x11,
   0x54, 0x00, 0x02, 0x00, 0x08, 0xa8, 0x10, 0x05, 0x1c,
   0x80, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0f, 0x12,
   0x54, 0x00, 0x4c, 0x00, 0x03, 0x00, 0x07, 0x00, 0x02,
   0x00, 0x00, 0x00, 0x60, 0x00, 0x0e, 0x00, 0x00, 0x00,
};

/* RT0 EOT Ioto program, loading the descriptor at PBE_ARGUMENT_VA. */
static const uint8_t eot_program[] = {
   0x0c, 0xa0, 0x02, 0x00, 0x08, 0x00, 0x10, 0x00, 0x1c,
   0x80, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x67, 0x00,
   0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00,
   0x40, 0x22, 0x00, 0x67, 0x00, 0x54, 0x08, 0x00, 0x00,
   0x00, 0x00, 0x17, 0x00, 0x02, 0x40, 0x22, 0x00, 0x77,
   0x00, 0x2a, 0x43, 0x4b, 0x00, 0x00, 0x00, 0x77, 0x01,
   0xaa, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0xf7,
   0x00, 0x2a, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x1c, 0x80, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14,
   0x81, 0x11, 0x06, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x80,
   0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x9f, 0x11, 0x54,
   0x00, 0x02, 0x00, 0x08, 0xa8, 0x10, 0x05, 0x1c, 0x80,
   0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0f, 0x12, 0x54,
   0x00, 0x4c, 0x00, 0x0b, 0x00, 0x01, 0x44, 0x1b, 0x02,
   0x01, 0x04, 0x2b, 0x04, 0x01, 0x04, 0x3b, 0x06, 0x01,
   0x04, 0x4b, 0x08, 0x01, 0x04, 0x5b, 0x0a, 0x01, 0x04,
   0x6b, 0x0c, 0x01, 0x04, 0x7b, 0x0e, 0x01, 0x04, 0x03,
   0x00, 0x07, 0x00, 0x02, 0x00, 0x00, 0x00, 0x60, 0x00,
   0x0e, 0x00, 0x00, 0x00,
};

/* Gen9, raw-twiddled BGRA mapping, 64x64 RGBA8_UNORM at TARGET_VA. */
static const uint8_t pbe_descriptor[] = {
   0x12, 0x0a, 0xc6, 0x3f, 0xc0, 0x0f, 0x00, 0x00,
   0x00, 0x88, 0x00, 0x00, 0x10, 0x00, 0x00, 0x10,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t vdm_terminate[] = {0x00, 0x00, 0x00, 0xc0};

struct mapped_bo {
   uint32_t handle;
   uint64_t size;
   uint64_t va;
   void *map;
};

static void
fail(const char *operation)
{
   fprintf(stderr, "asahi-submit-g17p-render: %s: %s\n", operation,
           strerror(errno));
   exit(EXIT_FAILURE);
}

static void
do_ioctl(int fd, unsigned long request, void *arg, const char *operation)
{
   if (ioctl(fd, request, arg) < 0)
      fail(operation);
}

static struct mapped_bo
create_and_bind(int fd, uint32_t vm_id, uint64_t va, uint64_t size)
{
   struct drm_asahi_gem_create create = {
      .size = size,
      .flags = DRM_ASAHI_GEM_WRITEBACK,
   };
   struct drm_asahi_gem_mmap_offset offset = {0};
   struct drm_asahi_gem_bind_op op = {
      .flags = DRM_ASAHI_BIND_READ | DRM_ASAHI_BIND_WRITE,
      .range = size,
      .addr = va,
   };
   struct drm_asahi_vm_bind bind = {
      .vm_id = vm_id,
      .num_binds = 1,
      .stride = sizeof(op),
      .userptr = (uintptr_t)&op,
   };
   struct mapped_bo bo = {.size = size, .va = va};

   do_ioctl(fd, DRM_IOCTL_ASAHI_GEM_CREATE, &create, "GEM_CREATE");
   bo.handle = create.handle;
   offset.handle = bo.handle;
   do_ioctl(fd, DRM_IOCTL_ASAHI_GEM_MMAP_OFFSET, &offset,
            "GEM_MMAP_OFFSET");
   bo.map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                 offset.offset);
   if (bo.map == MAP_FAILED)
      fail("mmap");
   op.handle = bo.handle;
   do_ioctl(fd, DRM_IOCTL_ASAHI_VM_BIND, &bind, "VM_BIND");
   return bo;
}

static uint64_t
fnv1a64(const uint8_t *bytes, size_t size)
{
   uint64_t hash = UINT64_C(14695981039346656037);
   for (size_t i = 0; i < size; ++i) {
      hash ^= bytes[i];
      hash *= UINT64_C(1099511628211);
   }
   return hash;
}

struct render_cmdbuf {
   struct drm_asahi_cmd_header attachment_header;
   struct drm_asahi_attachment attachment;
   struct drm_asahi_cmd_header render_header;
   struct drm_asahi_cmd_render render;
};

_Static_assert(sizeof(flat_fragment_shader) == 54,
               "flat fragment shader size changed");
_Static_assert(sizeof(eot_shader_text) == 128,
               "EOT shader text size changed");
_Static_assert(sizeof(bg_program) == 90, "background program size changed");
_Static_assert(sizeof(eot_program) == 166, "EOT program size changed");
_Static_assert(sizeof(pbe_descriptor) == 32, "PBE descriptor size changed");
_Static_assert(sizeof(struct drm_asahi_cmd_render) == 256,
               "render UAPI size changed");

static struct render_cmdbuf
build_render_commands(void)
{
   struct render_cmdbuf commands = {0};
   commands.attachment_header.cmd_type = DRM_ASAHI_SET_FRAGMENT_ATTACHMENTS;
   commands.attachment_header.size = sizeof(commands.attachment);
   commands.attachment_header.vdm_barrier = DRM_ASAHI_BARRIER_NONE;
   commands.attachment_header.cdm_barrier = DRM_ASAHI_BARRIER_NONE;
   commands.attachment.pointer = TARGET_VA;
   commands.attachment.size = TARGET_BYTES;

   commands.render_header.cmd_type = DRM_ASAHI_CMD_RENDER;
   commands.render_header.size = sizeof(commands.render);
   commands.render_header.vdm_barrier = DRM_ASAHI_BARRIER_NONE;
   commands.render_header.cdm_barrier = DRM_ASAHI_BARRIER_NONE;
   commands.render.flags = DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES |
                           DRM_ASAHI_RENDER_RSRC_SPEC_HI;
   commands.render.vdm_ctrl_stream_base = VDM_VA;
   commands.render.isp_scissor_base = SCISSOR_VA;
   commands.render.ppp_multisamplectl = UINT64_C(0x88);
   commands.render.ppp_ctrl = 0x202;
   commands.render.width_px = TARGET_WIDTH;
   commands.render.height_px = TARGET_HEIGHT;
   commands.render.layers = 1;
   commands.render.utile_width_px = 32;
   commands.render.utile_height_px = 32;
   commands.render.samples = 1;
   commands.render.sample_size_B = 8;
   commands.render.isp_merge_upper_x = UINT32_C(0x3cddb3d9);
   commands.render.isp_merge_upper_y = UINT32_C(0x3cddb3d9);
   commands.render.bg.usc = (uint32_t)(BG_PROGRAM_VA - USC_BASE) | 4;
   commands.render.bg.rsrc_spec = (uint32_t)BG_RESOURCE_SPEC;
   commands.render.eot.usc = (uint32_t)(EOT_PROGRAM_VA - USC_BASE) | 4;
   commands.render.eot.rsrc_spec = (uint32_t)EOT_RESOURCE_SPEC;
   commands.render.partial_bg = commands.render.bg;
   commands.render.partial_eot = commands.render.eot;
   commands.render.isp_bgobjdepth = UINT32_C(0x3f800000);
   commands.render.bg_rsrc_spec_hi = (uint32_t)(BG_RESOURCE_SPEC >> 32);
   commands.render.bg_partial_rsrc_spec_hi =
      (uint32_t)(BG_RESOURCE_SPEC >> 32);
   return commands;
}

static int
self_test(void)
{
   const struct render_cmdbuf commands = build_render_commands();
   uint8_t expected[TARGET_BYTES];
   static const uint8_t pixel[4] = {191, 128, 64, 255};
   for (size_t i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i)
      memcpy(&expected[i * 4], pixel, sizeof(pixel));

   const uint64_t encoded_target =
      ((uint64_t)pbe_descriptor[8] | ((uint64_t)pbe_descriptor[9] << 8) |
       ((uint64_t)pbe_descriptor[10] << 16) |
       ((uint64_t)pbe_descriptor[11] << 24) |
       ((uint64_t)pbe_descriptor[12] << 32) |
       ((uint64_t)pbe_descriptor[13] << 40))
      << 4;
   if (encoded_target != TARGET_VA ||
       memcmp(vdm_terminate, (const uint8_t[]){0, 0, 0, 0xc0}, 4) ||
       bg_program[sizeof(bg_program) - 4] != 0x0e ||
       eot_program[sizeof(eot_program) - 4] != 0x0e ||
       commands.render.bg.usc != UINT32_C(0x01990244) ||
       commands.render.eot.usc != UINT32_C(0x01990644) ||
       commands.render.bg_rsrc_spec_hi != UINT32_C(0x00078000) ||
       fnv1a64(expected, sizeof(expected)) != UINT64_C(0xe066f06356fa4325)) {
      fputs("asahi-submit-g17p-render: self-test failed\n", stderr);
      return EXIT_FAILURE;
   }

   puts("asahi-submit-g17p-render: self-test passed");
   return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
{
   if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
      return self_test();

   const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
   if (argc > 2) {
      fprintf(stderr, "usage: %s [render-node]\n", argv[0]);
      return EXIT_FAILURE;
   }

   struct drm_asahi_params_global params = {0};
   struct drm_asahi_get_params get = {
      .pointer = (uintptr_t)&params,
      .size = sizeof(params),
   };
   struct drm_asahi_vm_create vm = {
      .kernel_start = KERNEL_START,
      .kernel_end = KERNEL_END,
   };
   struct drm_asahi_queue_create queue = {
      .priority = DRM_ASAHI_PRIORITY_MEDIUM,
      .usc_exec_base = USC_BASE,
   };

   int fd = open(node, O_RDWR | O_CLOEXEC);
   if (fd < 0)
      fail("open render node");
   do_ioctl(fd, DRM_IOCTL_ASAHI_GET_PARAMS, &get, "GET_PARAMS");
   if (params.chip_id != 0x8140 || params.gpu_generation != 17 ||
       params.gpu_variant != 'P' || params.usc_generation != 3 ||
       params.gpu_hal_generation != DRM_ASAHI_GPU_HAL_200 ||
       params.max_commands_per_submission < 1) {
      fprintf(stderr,
              "asahi-submit-g17p-render: expected T8140/G17P/USC3/HAL200, "
              "got %#x/G%u/%c/USC%u/HAL%u maxcmd=%u\n",
              params.chip_id, params.gpu_generation,
              (char)params.gpu_variant, params.usc_generation,
              params.gpu_hal_generation, params.max_commands_per_submission);
      return EXIT_FAILURE;
   }

   do_ioctl(fd, DRM_IOCTL_ASAHI_VM_CREATE, &vm, "VM_CREATE");
   queue.vm_id = vm.vm_id;
   do_ioctl(fd, DRM_IOCTL_ASAHI_QUEUE_CREATE, &queue, "QUEUE_CREATE");

   struct mapped_bo code =
      create_and_bind(fd, vm.vm_id, CODE_PAGE_VA, GPU_PAGE_SIZE);
   struct mapped_bo vdm =
      create_and_bind(fd, vm.vm_id, VDM_VA, GPU_PAGE_SIZE);
   struct mapped_bo pbe =
      create_and_bind(fd, vm.vm_id, PBE_PAGE_VA, GPU_PAGE_SIZE);
   struct mapped_bo target =
      create_and_bind(fd, vm.vm_id, TARGET_VA, GPU_PAGE_SIZE);
   struct mapped_bo scissor =
      create_and_bind(fd, vm.vm_id, SCISSOR_VA, GPU_PAGE_SIZE);
   struct mapped_bo pipeline =
      create_and_bind(fd, vm.vm_id, PIPELINE_PAGE_VA, GPU_PAGE_SIZE);

   memset(code.map, 0, code.size);
   memcpy((uint8_t *)code.map + 0x80, flat_fragment_shader,
          sizeof(flat_fragment_shader));
   memcpy((uint8_t *)code.map + 0x100, eot_shader_text,
          sizeof(eot_shader_text));
   memset(vdm.map, 0, vdm.size);
   memcpy(vdm.map, vdm_terminate, sizeof(vdm_terminate));
   memset(pbe.map, 0, pbe.size);
   memcpy((uint8_t *)pbe.map + (PBE_ARGUMENT_VA - PBE_PAGE_VA),
          pbe_descriptor, sizeof(pbe_descriptor));
   memset(target.map, 0, target.size);
   memset(scissor.map, 0, scissor.size);
   memset(pipeline.map, 0, pipeline.size);
   memcpy((uint8_t *)pipeline.map + (BG_PROGRAM_VA - PIPELINE_PAGE_VA),
          bg_program, sizeof(bg_program));
   memcpy((uint8_t *)pipeline.map + (EOT_PROGRAM_VA - PIPELINE_PAGE_VA),
          eot_program, sizeof(eot_program));
   __sync_synchronize();

   struct render_cmdbuf commands = build_render_commands();

   struct drm_asahi_submit submit = {
      .cmdbuf = (uintptr_t)&commands,
      .queue_id = queue.queue_id,
      .cmdbuf_size = sizeof(commands),
   };
   puts("asahi-submit-g17p-render: submitting one empty-tile RT0 pass");
   do_ioctl(fd, DRM_IOCTL_ASAHI_SUBMIT, &submit, "SUBMIT");
   __sync_synchronize();

   static const uint8_t expected_pixel[4] = {191, 128, 64, 255};
   const uint8_t *bytes = target.map;
   for (size_t pixel = 0; pixel < TARGET_WIDTH * TARGET_HEIGHT; ++pixel) {
      const uint8_t *actual = &bytes[pixel * 4];
      if (memcmp(actual, expected_pixel, sizeof(expected_pixel)) != 0) {
         fprintf(stderr,
                 "asahi-submit-g17p-render: pixel %zu is {%u,%u,%u,%u}, "
                 "expected {191,128,64,255}\n",
                 pixel, actual[0], actual[1], actual[2], actual[3]);
         return EXIT_FAILURE;
      }
   }

   printf("asahi-submit-g17p-render: PASS — 64x64 raw-twiddled RT0, "
          "checksum %016" PRIx64 "\n",
          fnv1a64(bytes, TARGET_BYTES));
   close(fd);
   return EXIT_SUCCESS;
}
