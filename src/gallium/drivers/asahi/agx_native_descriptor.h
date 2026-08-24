/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asahi/lib/agx_chip.h"

#include <stdbool.h>
#include <stdint.h>

/* Gallium used struct agx_texture_packed as both the descriptor and its array
 * geometry.  That happens to be correct for Gen7/8, where texture and PBE
 * descriptors are 24 bytes, but it aliases adjacent entries once Gen9 grows
 * both descriptors to 32 bytes.  Keep the native allocation opaque so a C
 * type can never silently select the AGX2 stride again.
 */
#define AGX_GALLIUM_DESCRIPTOR_AGX2_SIZE 24u
#define AGX_GALLIUM_DESCRIPTOR_AGX3_SIZE 32u
#define AGX_GALLIUM_DESCRIPTOR_MAX_SIZE  AGX_GALLIUM_DESCRIPTOR_AGX3_SIZE

struct agx_gallium_texture_descriptor {
   uint32_t opaque[AGX_GALLIUM_DESCRIPTOR_MAX_SIZE / 4];
};

struct agx_gallium_pbe_descriptor {
   uint32_t opaque[AGX_GALLIUM_DESCRIPTOR_MAX_SIZE / 4];
};

struct agx_gallium_storage_descriptor_layout {
   uint8_t texture;
   uint8_t pbe;
   uint8_t stride;
};

/* G15P/A16 executes AGX2, but its complete descriptor/command-stream ABI is
 * not grounded.  Keep it UNKNOWN here just like the shared chip policy.
 */
static inline enum agx_descriptor_generation
agx_gallium_descriptor_generation(enum agx_chip chip)
{
   switch (chip) {
   case AGX_CHIP_G13G:
   case AGX_CHIP_G13X:
      return AGX_DESCRIPTOR_GENERATION_7;
   case AGX_CHIP_G14G:
   case AGX_CHIP_G14X:
      return AGX_DESCRIPTOR_GENERATION_8;
   case AGX_CHIP_G15G:
   case AGX_CHIP_G15S:
   case AGX_CHIP_G15C:
   case AGX_CHIP_G15D:
   case AGX_CHIP_G16P:
   case AGX_CHIP_G16G:
   case AGX_CHIP_G16S:
   case AGX_CHIP_G16C:
   case AGX_CHIP_G17P:
   case AGX_CHIP_G17A:
      return AGX_DESCRIPTOR_GENERATION_9;
   case AGX_CHIP_G17G:
   case AGX_CHIP_G17S:
   case AGX_CHIP_G17C:
      return AGX_DESCRIPTOR_GENERATION_10;
   case AGX_CHIP_G15P:
   case AGX_CHIP_UNKNOWN:
   default:
      return AGX_DESCRIPTOR_GENERATION_UNKNOWN;
   }
}

static inline bool
agx_gallium_descriptor_generation_is_agx2(
   enum agx_descriptor_generation generation)
{
   return generation == AGX_DESCRIPTOR_GENERATION_7 ||
          generation == AGX_DESCRIPTOR_GENERATION_8;
}

static inline bool
agx_gallium_descriptor_generation_is_agx3(
   enum agx_descriptor_generation generation)
{
   return generation == AGX_DESCRIPTOR_GENERATION_9 ||
          generation == AGX_DESCRIPTOR_GENERATION_10;
}

static inline unsigned
agx_gallium_texture_descriptor_size(
   enum agx_descriptor_generation generation)
{
   return agx_gallium_descriptor_generation_is_agx2(generation)
             ? AGX_GALLIUM_DESCRIPTOR_AGX2_SIZE
          : agx_gallium_descriptor_generation_is_agx3(generation)
             ? AGX_GALLIUM_DESCRIPTOR_AGX3_SIZE
             : 0;
}

static inline unsigned
agx_gallium_pbe_descriptor_size(enum agx_descriptor_generation generation)
{
   return agx_gallium_texture_descriptor_size(generation);
}

static inline struct agx_gallium_storage_descriptor_layout
agx_gallium_storage_descriptor_layout(
   enum agx_descriptor_generation generation)
{
   const unsigned texture = agx_gallium_texture_descriptor_size(generation);
   const unsigned pbe = agx_gallium_pbe_descriptor_size(generation);

   if (!texture || !pbe)
      return (struct agx_gallium_storage_descriptor_layout){0};

   return (struct agx_gallium_storage_descriptor_layout){
      .texture = 0,
      .pbe = texture,
      .stride = texture + pbe,
   };
}

/* Gen9's multisample selector is not yet identified.  AGX3 buffer texture
 * descriptors also lack the recovered equivalent of AGX2's software tail
 * offset.  Never encode either case using legacy bit positions.
 */
static inline bool
agx_gallium_native_texture_supported(
   enum agx_descriptor_generation generation, bool multisampled,
   unsigned buffer_tail_offset)
{
   if (agx_gallium_descriptor_generation_is_agx2(generation))
      return true;

   if (!agx_gallium_descriptor_generation_is_agx3(generation))
      return false;

   if (generation == AGX_DESCRIPTOR_GENERATION_9 && multisampled)
      return false;

   return buffer_tail_offset == 0;
}

/* The native Gen9/10 PBE words are structurally known, but Gallium's image
 * paths consume AGX2 software sideband state that has not been mapped.  Keep
 * render-target stores and writable images fail-closed until that ABI lands.
 */
static inline bool
agx_gallium_native_pbe_supported(
   enum agx_descriptor_generation generation)
{
   return agx_gallium_descriptor_generation_is_agx2(generation);
}

struct AGX_TEXTURE;
struct agx_pbe_packed;

bool agx_gallium_pack_native_texture(
   enum agx_descriptor_generation generation,
   struct agx_gallium_texture_descriptor *out,
   const struct AGX_TEXTURE *semantic);

bool agx_gallium_set_null_native_texture(
   enum agx_descriptor_generation generation,
   struct agx_gallium_texture_descriptor *out);

bool agx_gallium_pack_native_pbe(
   enum agx_descriptor_generation generation,
   struct agx_gallium_pbe_descriptor *out,
   const struct agx_pbe_packed *legacy);

bool agx_gallium_set_null_native_pbe(
   enum agx_descriptor_generation generation,
   struct agx_gallium_pbe_descriptor *out);
