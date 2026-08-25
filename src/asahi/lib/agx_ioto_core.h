/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "agx_chip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_IOTO_MAX_RECORD_SIZE 22

/*
 * Raw Ioto encoding points recovered from Apple's G15/G16/G17P and HAL300
 * emitters.  These names identify byte forms only; they assign no semantics
 * to the records' anonymous or opaque fields.
 */
enum agx_ioto_kind {
   AGX_IOTO_KIND_INVALID = 0,
   AGX_IOTO_KIND_NOP_EP0,
   AGX_IOTO_KIND_NOP_EP1,
   AGX_IOTO_KIND_NOP_EP2,
   AGX_IOTO_KIND_NOP_EP3,
   AGX_IOTO_KIND_NOP_EP4,
   AGX_IOTO_KIND_NOP_EP5,
   AGX_IOTO_KIND_NOP_EP6,
   AGX_IOTO_KIND_NOP_EP7,
   AGX_IOTO_KIND_NOP_EP8,
   AGX_IOTO_KIND_NOP_EP9,
   AGX_IOTO_KIND_NOP_EP10,
   AGX_IOTO_KIND_LDIMM_EP0,
   AGX_IOTO_KIND_LDIMM_EP1,
   AGX_IOTO_KIND_LDIMM_EP2,
   AGX_IOTO_KIND_LDIMM_EP3,
   AGX_IOTO_KIND_COPY_EP0,
   AGX_IOTO_KIND_COPY_EP1,
   AGX_IOTO_KIND_RLD_EP0,
   AGX_IOTO_KIND_RLD_EP1,
   AGX_IOTO_KIND_SPECLM_EP0,
   AGX_IOTO_KIND_LDSHDR_EP0,
   AGX_IOTO_KIND_SPECTPR_EP0,
   AGX_IOTO_KIND_IADD_EP0,
   AGX_IOTO_KIND_B_EP0,
   AGX_IOTO_KIND_B_EP1,
   AGX_IOTO_KIND_B_EP1_COMPACT,
   AGX_IOTO_KIND_BL_EP0,
   AGX_IOTO_KIND_BL_EP1,
   AGX_IOTO_KIND_BL_EP1_COMPACT,
   AGX_IOTO_KIND_CNDCALL_EP0,
   AGX_IOTO_KIND_CNDRET_EP0,
   AGX_IOTO_KIND_LDDEV_EP0,
   AGX_IOTO_KIND_LDDEV_EP1,
   AGX_IOTO_KIND_LDDEV_EP2,
   AGX_IOTO_KIND_LDDEV_EP3,
   AGX_IOTO_KIND_LDDEV_EP4,
   AGX_IOTO_KIND_LDDEV_EP5,
   AGX_IOTO_KIND_PIXREL_EP0,
   AGX_IOTO_KIND_PIXWAIT_EP0,
   AGX_IOTO_KIND_SETPROFILECTL_EP0,
   AGX_IOTO_KIND_COUNT,
};

struct agx_ioto_record {
   enum agx_ioto_kind kind;
   uint8_t length;
   uint8_t bytes[AGX_IOTO_MAX_RECORD_SIZE];
};

/* Exactly the eleven byte-witnessed M3/M4/A18 Pro/M5 identities. */
bool agx_ioto_target_supported(enum agx_chip chip);

/* Return the target-specific exact record length, or zero if unsupported. */
size_t agx_ioto_kind_size(enum agx_chip chip, enum agx_ioto_kind kind);

/*
 * Validate one expected record at the start of a possibly longer stream.
 * Opaque bytes are copied byte-for-byte to out and consumed is the exact
 * target-specific record length.  Both outputs are cleared on failure.
 */
bool agx_ioto_validate_kind(enum agx_chip chip,
                            enum agx_ioto_kind expected, const void *stream,
                            size_t available, struct agx_ioto_record *out,
                            size_t *consumed);

/*
 * Decode only when exactly one target-valid form matches.  A truncated prefix
 * of any other form is treated as ambiguity and rejected rather than guessed.
 * Both outputs are cleared on failure.
 */
bool agx_ioto_decode_one(enum agx_chip chip, const void *stream,
                         size_t available, struct agx_ioto_record *out,
                         size_t *consumed);

/*
 * Copy a target-valid raw record without interpreting or rewriting opaque
 * bits.  Failure leaves the destination unchanged.
 */
bool agx_ioto_record_copy(enum agx_chip chip,
                          const struct agx_ioto_record *record, void *out,
                          size_t out_size);

#ifdef __cplusplus
}
#endif
