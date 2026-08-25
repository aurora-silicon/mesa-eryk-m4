/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded host-side model of Apple's AGX3 EslBuilder absolute-load scheduler.
 *
 * This core owns no memory and is deliberately not a production enablement
 * gate.  LDIMM, allocator, cache, ordering, two-phase finish and G15-style
 * NOP padding are encoded here.  The still-anonymous LDDEV/COPY operands are
 * supplied as exact native records by callbacks and validated before append.
 */

#pragma once

#include "agx_chip.h"
#include "agx_ioto_core.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_IOTO_LOAD_LDIMM_MAX_SIZE      8u
#define AGX_IOTO_LOAD_INLINE_LDDEV14_SIZE 14u
#define AGX_IOTO_LOAD_GPU1_LDDEV_MAX_SIZE 16u
#define AGX_IOTO_LOAD_GPU0_COPY_MAX_SIZE  10u
#define AGX_IOTO_LOAD_GPU1_COPY_MAX_SIZE  8u
#define AGX_IOTO_LOAD_NOP_SIZE            2u
#define AGX_IOTO_LOAD_GPU0_ROUND_NOP_SIZE 8u

enum agx_ioto_load_encoding {
   AGX_IOTO_LOAD_ENCODING_UNSUPPORTED = 0,
   AGX_IOTO_LOAD_ENCODING_GPU0,
   AGX_IOTO_LOAD_ENCODING_GPU1,
};

struct agx_ioto_load_sink {
   uint8_t *data;
   size_t capacity;
   size_t length;
};

/* Apple Register objects compare by the tuple documented on this structure. */
struct agx_ioto_load_register {
   uint16_t half;
   uint32_t type;
   int32_t value;
};

struct agx_ioto_inflight_load {
   struct agx_ioto_load_register data;
   struct agx_ioto_load_register base;
   int16_t byte_offset;
   uint8_t count_dwords;
   uint16_t native_slot_half;
   uint8_t lane_mask;
   uint16_t cprog_dest_half;

   uint32_t sequence;
   uint8_t signal_slot;
   bool processed;
};

struct agx_ioto_load_cache_entry {
   uint64_t address;
   uint16_t base_half;
};

struct agx_ioto_load_workspace {
   struct agx_ioto_inflight_load *loads;
   size_t capacity;
};

/* GPU0 does not expose AGXIotoInstruction_LDDEV.  Its EslBuilder writes the
 * AGX3b lddev instruction inline as one fixed 14-byte record. */
enum agx_ioto_load_raw_kind {
   AGX_IOTO_LOAD_RAW_INVALID = 0,
   AGX_IOTO_GPU0_INLINE_LDDEV14,
   AGX_IOTO_LOAD_RAW_LDDEV_EP0,
   AGX_IOTO_LOAD_RAW_LDDEV_EP1,
   AGX_IOTO_LOAD_RAW_LDDEV_EP2,
   AGX_IOTO_LOAD_RAW_LDDEV_EP3,
   AGX_IOTO_LOAD_RAW_LDDEV_EP4,
   AGX_IOTO_LOAD_RAW_LDDEV_EP5,
   AGX_IOTO_LOAD_RAW_COPY_EP0,
   AGX_IOTO_LOAD_RAW_COPY_EP1,
};

struct agx_ioto_load_raw_record {
   enum agx_ioto_load_raw_kind kind;
   uint8_t length;
   uint8_t bytes[AGX_IOTO_LOAD_GPU1_LDDEV_MAX_SIZE];
};

/*
 * Callbacks are the narrow fail-closed boundary around fields whose native
 * semantic names are not yet established.  A callback must return one exact
 * target-native record.  The core validates its length and recovered fixed
 * bits before appending it transactionally.
 */
struct agx_ioto_load_emit_ops {
   bool (*pack_load)(void *context, enum agx_chip chip,
                     const struct agx_ioto_inflight_load *load,
                     struct agx_ioto_load_raw_record *record,
                     uint8_t *signal_slot);
   bool (*pack_copy)(void *context, enum agx_chip chip,
                     const struct agx_ioto_inflight_load *load, uint8_t dword,
                     uint8_t lane_code,
                     struct agx_ioto_load_raw_record *record);
   void *context;
};

struct agx_ioto_load_core {
   enum agx_chip chip;
   enum agx_ioto_load_encoding encoding;
   struct agx_ioto_load_sink sink;

   struct agx_ioto_inflight_load *loads;
   size_t load_count;
   size_t load_capacity;

   struct agx_ioto_load_cache_entry *cache;
   size_t cache_count;
   size_t cache_capacity;

   uint32_t next_sequence;
   uint8_t initial_gpr_dwords;
   uint8_t current_gpr_dwords;
   bool fast_path_enabled;
   bool gpu0_round_nops;
};

struct agx_ioto_load_absolute {
   uint32_t native_slot;
   uint8_t count_dwords;
   uint64_t address;
   uint8_t lane_mask;
   uint16_t cprog_dest_half;
};

struct agx_ioto_load_allocation {
   uint16_t temp_half;
   uint8_t reserved_dwords;
   bool fast_path;
};

enum agx_ioto_load_alloc_result {
   AGX_IOTO_LOAD_ALLOC_INVALID = 0,
   AGX_IOTO_LOAD_ALLOC_OK,
   AGX_IOTO_LOAD_ALLOC_NEEDS_ROUND,
};

enum agx_ioto_load_encoding agx_ioto_load_target_encoding(enum agx_chip chip);

/* Remains false until native LDDEV/COPY semantic packers replace callbacks. */
bool agx_ioto_load_production_ready(enum agx_chip chip);

bool agx_ioto_load_core_init(struct agx_ioto_load_core *core,
                             enum agx_chip chip, struct agx_ioto_load_sink sink,
                             struct agx_ioto_inflight_load *loads,
                             size_t load_capacity,
                             struct agx_ioto_load_cache_entry *cache,
                             size_t cache_capacity, uint8_t initial_gpr_dwords,
                             bool fast_path_enabled, bool gpu0_round_nops);

bool agx_ioto_load_pack_ldimm(enum agx_chip chip, uint16_t dest_half,
                              uint32_t immediate,
                              uint8_t out[AGX_IOTO_LOAD_LDIMM_MAX_SIZE],
                              uint8_t *length);

void agx_ioto_load_pack_nop(uint8_t hazard_mask,
                            uint8_t out[AGX_IOTO_LOAD_NOP_SIZE]);

/* Pure allocator step.  NEEDS_ROUND and INVALID leave core unchanged. */
enum agx_ioto_load_alloc_result
agx_ioto_load_allocator_reserve(struct agx_ioto_load_core *core,
                                uint8_t requested_dwords,
                                uint16_t cprog_dest_half, uint32_t lane_mask,
                                struct agx_ioto_load_allocation *allocation);

/* Exact Apple comparator: {key(base), key(data)}. */
int agx_ioto_load_compare(const struct agx_ioto_inflight_load *a,
                          const struct agx_ioto_inflight_load *b);

/* Maximum bytes a finish call may append for the current pending set. */
bool agx_ioto_load_finish_worst_case_size(const struct agx_ioto_load_core *core,
                                          bool complete_round,
                                          size_t *required);

/* false emits/marks only loads; true also resets allocation, emits the GPU0
 * four-NOP pad when configured, emits copies, and consumes pending loads. */
bool agx_ioto_load_finish_round(struct agx_ioto_load_core *core,
                                bool complete_round,
                                const struct agx_ioto_load_emit_ops *ops,
                                struct agx_ioto_load_workspace workspace);

/* Queue one compiler-native absolute load.  All logical state and sink length
 * roll back on failure.  Counts above four are rejected here because Apple's
 * queued lane mask is one byte; allocator boundary coverage uses reserve(). */
bool agx_ioto_load_absolute(struct agx_ioto_load_core *core,
                            const struct agx_ioto_load_absolute *load,
                            const struct agx_ioto_load_emit_ops *ops,
                            struct agx_ioto_load_workspace workspace,
                            struct agx_ioto_load_allocation *allocation);

#ifdef __cplusplus
}
#endif
