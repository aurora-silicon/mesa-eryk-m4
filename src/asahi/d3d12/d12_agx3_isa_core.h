/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Bounded D3D12 scalar-ALU mapping into the source-grounded AGX3 packer.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "asahi/lib/agx_chip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define D12_AGX3_BINARY_ALU_MAX_LENGTH 14

/* These are D3D/DXIL scalar semantics, not raw Apple opcode numbers. */
enum d12_agx3_binary_alu_op {
   D12_AGX3_BINARY_ALU_IADD_U32 = 0,
   D12_AGX3_BINARY_ALU_IAND_U32 = 1,
   D12_AGX3_BINARY_ALU_IOR_U32 = 2,
   D12_AGX3_BINARY_ALU_IXOR_U32 = 3,
   D12_AGX3_BINARY_ALU_ISUB_U32 = 4,
   D12_AGX3_BINARY_ALU_ISHL_U32 = 5,
   D12_AGX3_BINARY_ALU_IASHR_I32 = 6,
};

enum d12_agx3_unary_alu_op {
   D12_AGX3_UNARY_ALU_INOT_U32 = 0,
};

struct d12_agx3_binary_alu_encoding {
   uint8_t bytes[D12_AGX3_BINARY_ALU_MAX_LENGTH];
   uint8_t length;
};

struct d12_agx3_unary_alu_encoding {
   uint8_t bytes[D12_AGX3_BINARY_ALU_MAX_LENGTH];
   uint8_t length;
};

/*
 * Encode one canonical three-register scalar operation. Register arguments
 * are Mesa half-register indices, so 32-bit operands must be even. This is a
 * compiler building block only: success does not imply that AGX3 pipeline
 * compilation, scheduling, command submission, or device admission exists.
 * ISHL and IASHR map only the proved register-register `sll` and `sra`
 * instructions; they do not emit or claim the separate shift-count masking
 * seen in compiler output.
 */
bool d12_agx3_encode_binary_alu(
   enum agx_isa_encoding encoding, enum d12_agx3_binary_alu_op op,
   uint16_t dest, uint16_t src0, uint16_t src1,
   struct d12_agx3_binary_alu_encoding *out);

/* Same bounded contract as the binary bridge, for one-register semantics. */
bool d12_agx3_encode_unary_alu(
   enum agx_isa_encoding encoding, enum d12_agx3_unary_alu_op op,
   uint16_t dest, uint16_t src, struct d12_agx3_unary_alu_encoding *out);

#ifdef __cplusplus
}
#endif
