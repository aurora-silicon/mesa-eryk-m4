/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * AGX3 scoreboard slot assignment.
 *
 * AGX3 has no standalone wait instruction and no AGX2-style two-slot scoreboard.
 * Instead every async result (device/threadgroup load, texture sample, atomic,
 * stack fill) is tagged with a scoreboard slot, and every later instruction that
 * consumes such a result carries a wait-bit mask naming the slots it must first
 * synchronise. A slot waited once is satisfied for all later instructions until
 * it is reallocated ("waited once = satisfied globally"). ALU-to-ALU
 * dependencies are hardware-interlocked and need no wait.
 *
 * This is the encoding-independent algorithm -- which async op gets which slot,
 * and which slots each instruction waits -- expressed over a normalized
 * instruction list so it is testable on its own. The wait-bit field ids per
 * mnemonic (agx3_wait_field_map) and the actual deposit are the caller's.
 * Evidence: research/gpu/GPU_AGX3_LOCAL_STACK_WAIT_2026-08-24.md S6.3.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Up to 16 half-register sources/dests per instruction is far more than any AGX3
 * form uses. */
#define AGX3_SLOT_MAX_REGS 16u

struct agx3_slot_instr {
   bool is_async;   /* produces a scoreboard-tracked result */
   uint8_t nr_dsts; /* half-register indices this instruction writes */
   uint8_t nr_srcs; /* half-register indices this instruction reads */
   uint16_t dst[AGX3_SLOT_MAX_REGS];
   uint16_t src[AGX3_SLOT_MAX_REGS];
};

struct agx3_slot_result {
   uint8_t slot;       /* 1..nslots for async instructions, 0 otherwise */
   uint16_t wait_mask; /* bit (s-1) set => wait scoreboard slot s */
};

/*
 * Assign slots and waits for one straight-line instruction region, which may
 * span fallthrough basic blocks. `n` is the instruction count; `nslots` is 6
 * (agx3b) or 8 (agx3c). Writes one result per instruction. Returns false on a
 * malformed input (too many regs, nslots out of range, null pointers).
 *
 * Registers are half-register indices; a value that is not currently the live
 * result of an async op contributes no wait. The slot policy is round-robin,
 * which the RE showed is always correct (reuse only forces an earlier wait,
 * never misses a hazard).
 */
bool agx3_slot_assign(const struct agx3_slot_instr *instrs, unsigned n,
                      unsigned nslots, struct agx3_slot_result *out);

#ifdef __cplusplus
}
#endif
