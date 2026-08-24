/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "../agx_texture_side_entry.h"

#include <stdio.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition)                                                       \
   do {                                                                        \
      ++checks;                                                                \
      if (!(condition)) {                                                      \
         fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);       \
         ++failures;                                                           \
      }                                                                        \
   } while (0)

#define CHECK_PACK_FAIL(info)                                                  \
   do {                                                                        \
      uint64_t failed_pack = UINT64_MAX;                                       \
      CHECK(!agx_texture_side_entry_pack((info), &failed_pack));               \
      CHECK(failed_pack == 0);                                                 \
   } while (0)

static struct agx_texture_side_entry_info
make_info(uint32_t generation, enum agx_texture_side_entry_kind kind)
{
   struct agx_texture_side_entry_info info = {0};
   info.generation = generation;
   info.form = AGX_TEXTURE_SIDE_ENTRY_FORM_NORMAL;
   info.kind = kind;
   info.auxiliary_heap_index = UINT32_C(0xa1b2c3d4);
   return info;
}

static struct agx_texture_side_entry_gen9_atomic_info
make_complex_atomic_info(
   enum agx_texture_side_entry_gen9_atomic_class layout_class)
{
   return (struct agx_texture_side_entry_gen9_atomic_info){
      .eligible = true,
      .layout_class = layout_class,
      .width_texels = 257,
      .height_texels = 129,
      .mip_size_bytes = UINT64_C(0x100000),
      .level_offset_bytes = UINT64_C(0x24000),
      .base_alignment_bytes = 128,
      .format_element_bytes = 4,
   };
}

static void
test_raw_geometry(void)
{
   uint64_t packed = 0;
   struct agx_texture_side_entry_decoded decoded = {0};

   CHECK(AGX_TEXTURE_SIDE_ENTRY_SIZE == sizeof(uint64_t));
   CHECK(agx_texture_side_entry_compose(UINT32_C(0x11223344),
                                        UINT32_C(0xaabbccdd), &packed));
   CHECK(packed == UINT64_C(0xaabbccdd11223344));
   CHECK(agx_texture_side_entry_decode(packed, &decoded));
   CHECK(decoded.payload == UINT32_C(0x11223344));
   CHECK(decoded.auxiliary_heap_index == UINT32_C(0xaabbccdd));

   CHECK(agx_texture_side_entry_compose(0, UINT32_MAX, &packed));
   CHECK(packed == UINT64_C(0xffffffff00000000));
   CHECK(agx_texture_side_entry_decode(UINT64_MAX, &decoded));
   CHECK(decoded.payload == UINT32_MAX);
   CHECK(decoded.auxiliary_heap_index == UINT32_MAX);
   CHECK(!agx_texture_side_entry_compose(1, 2, NULL));
   CHECK(!agx_texture_side_entry_decode(0, NULL));
}

static void
test_buffer_entries(void)
{
   const uint32_t generations[] = {
      AGX_TEXTURE_SIDE_ENTRY_GENERATION_9,
      AGX_TEXTURE_SIDE_ENTRY_GENERATION_10,
   };

   for (unsigned i = 0; i < sizeof(generations) / sizeof(generations[0]); ++i) {
      struct agx_texture_side_entry_info info =
         make_info(generations[i], AGX_TEXTURE_SIDE_ENTRY_KIND_BUFFER);
      uint64_t packed = 0;

      info.value.buffer_width_texels = 4096;
      CHECK(agx_texture_side_entry_pack(&info, &packed));
      CHECK(packed == UINT64_C(0xa1b2c3d400001000));

      info.value.buffer_width_texels = UINT32_MAX;
      CHECK(agx_texture_side_entry_pack(&info, &packed));
      CHECK(packed == UINT64_C(0xa1b2c3d4ffffffff));

      info.value.buffer_width_texels = 0;
      CHECK_PACK_FAIL(&info);
      info.value.buffer_width_texels = UINT64_C(0x100000000);
      CHECK_PACK_FAIL(&info);
   }
}

static void
test_gen10_ordinary_image(void)
{
   struct agx_texture_side_entry_info info =
      make_info(AGX_TEXTURE_SIDE_ENTRY_GENERATION_10,
                AGX_TEXTURE_SIDE_ENTRY_KIND_ORDINARY_IMAGE);
   uint64_t packed = 0;

   CHECK(agx_texture_side_entry_pack(&info, &packed));
   CHECK(packed == UINT64_C(0xa1b2c3d400000000));

   info.generation = AGX_TEXTURE_SIDE_ENTRY_GENERATION_9;
   CHECK_PACK_FAIL(&info);
}

static void
test_simple_gen9_atomic_payloads(void)
{
   struct agx_texture_side_entry_gen9_atomic_info atomic = {
      .eligible = true,
      .layout_class = AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_1D,
      .level_offset_bytes = UINT64_C(0x123456780),
   };
   uint32_t payload = UINT32_MAX;

   CHECK(agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   CHECK(payload == UINT32_C(0x02468acf));

   atomic.layout_class = AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_2D;
   atomic.level_offset_bytes = 127;
   CHECK(agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   CHECK(payload == 0);
   atomic.level_offset_bytes = 128;
   CHECK(agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   CHECK(payload == 1);

   atomic.level_offset_bytes = UINT64_C(0x8000000000);
   payload = UINT32_MAX;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   CHECK(payload == 0);

   atomic.level_offset_bytes = 0;
   atomic.eligible = false;
   payload = UINT32_MAX;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   CHECK(payload == 0);
}

static void
test_complex_gen9_atomic_payloads(void)
{
   uint32_t payload = UINT32_MAX;

   /* 257*129*4 rounds from 132612 to a 147456-byte plane. */
   struct agx_texture_side_entry_gen9_atomic_info atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   CHECK(agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   CHECK(payload == UINT32_C(0x2000ae99));

   atomic.layout_class = AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_1D_ARRAY;
   atomic.base_alignment_bytes = UINT64_C(0x4000);
   atomic.level_offset_bytes = UINT64_C(0x8000);
   CHECK(agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   CHECK(payload == UINT32_C(0x0037003e));

   atomic.layout_class = AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_2D_ARRAY;
   atomic.level_offset_bytes = UINT64_C(0x24080);
   CHECK(agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   CHECK(payload == UINT32_C(0x0037ae9a));

   atomic.format_element_bytes = 8;
   atomic.width_texels = 128;
   atomic.height_texels = 128;
   atomic.level_offset_bytes = UINT64_C(0x20080);
   CHECK(agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   CHECK(payload == UINT32_C(0x0038ae9a));
}

static void
test_atomic_validation(void)
{
   struct agx_texture_side_entry_gen9_atomic_info atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   uint32_t payload = UINT32_MAX;

   atomic.layout_class = (enum agx_texture_side_entry_gen9_atomic_class)0x400;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   CHECK(payload == 0);

   atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   atomic.width_texels = 0;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   atomic.width_texels = UINT64_C(0x100000000);
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));

   atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   atomic.height_texels = 0;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   atomic.height_texels = UINT64_C(0x100000000);
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));

   atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   atomic.format_element_bytes = 1;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   atomic.format_element_bytes = 16;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));

   atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   atomic.base_alignment_bytes = 64;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));
   atomic.base_alignment_bytes = 192;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));

   atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   atomic.width_texels = UINT32_MAX;
   atomic.height_texels = UINT32_MAX;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));

   atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   atomic.base_alignment_bytes = UINT64_C(0x4000);
   atomic.mip_size_bytes = UINT64_C(0x20000);
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));

   atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   atomic.base_alignment_bytes = UINT64_C(0x4000);
   atomic.level_offset_bytes = UINT64_C(0x10080);
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));

   atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   atomic.mip_size_bytes = UINT64_C(0x1000000);
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));

   atomic =
      make_complex_atomic_info(AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_3D);
   atomic.level_offset_bytes = UINT64_C(0x400000);
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, &payload));

   payload = UINT32_MAX;
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(NULL, &payload));
   CHECK(payload == 0);
   CHECK(!agx_texture_side_entry_pack_gen9_atomic_payload(&atomic, NULL));
}

static void
test_semantic_validation(void)
{
   struct agx_texture_side_entry_info info =
      make_info(AGX_TEXTURE_SIDE_ENTRY_GENERATION_9,
                AGX_TEXTURE_SIDE_ENTRY_KIND_GEN9_ATOMIC_IMAGE);
   uint64_t packed = 0;

   info.value.gen9_atomic = (struct agx_texture_side_entry_gen9_atomic_info){
      .eligible = true,
      .layout_class = AGX_TEXTURE_SIDE_ENTRY_GEN9_ATOMIC_CLASS_1D,
      .level_offset_bytes = UINT64_C(0x4000),
   };
   CHECK(agx_texture_side_entry_pack(&info, &packed));
   CHECK(packed == UINT64_C(0xa1b2c3d400000080));

   info.generation = AGX_TEXTURE_SIDE_ENTRY_GENERATION_10;
   CHECK_PACK_FAIL(&info);
   info.generation = 8;
   CHECK_PACK_FAIL(&info);
   info.generation = 11;
   CHECK_PACK_FAIL(&info);

   info = make_info(AGX_TEXTURE_SIDE_ENTRY_GENERATION_10,
                    AGX_TEXTURE_SIDE_ENTRY_KIND_BUFFER);
   info.value.buffer_width_texels = 1;
   info.form = AGX_TEXTURE_SIDE_ENTRY_FORM_DIRECT_UNRESOLVED;
   CHECK_PACK_FAIL(&info);
   info.form = (enum agx_texture_side_entry_form)1;
   CHECK_PACK_FAIL(&info);

   info.form = AGX_TEXTURE_SIDE_ENTRY_FORM_NORMAL;
   info.kind = AGX_TEXTURE_SIDE_ENTRY_KIND_UNKNOWN;
   CHECK_PACK_FAIL(&info);
   info.kind = (enum agx_texture_side_entry_kind)99;
   CHECK_PACK_FAIL(&info);

   packed = UINT64_MAX;
   CHECK(!agx_texture_side_entry_pack(NULL, &packed));
   CHECK(packed == 0);
   CHECK(!agx_texture_side_entry_pack(&info, NULL));
}

int
main(void)
{
   test_raw_geometry();
   test_buffer_entries();
   test_gen10_ordinary_image();
   test_simple_gen9_atomic_payloads();
   test_complex_gen9_atomic_payloads();
   test_atomic_validation();
   test_semantic_validation();

   if (failures) {
      fprintf(stderr, "AGX texture side entry: %u of %u checks failed\n",
              failures, checks);
      return 1;
   }

   printf("AGX texture side entry: all %u checks passed\n", checks);
   return 0;
}
