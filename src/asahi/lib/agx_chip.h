/*
 * Copyright 2026 The Asahi Linux Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * Apple GPU identity, instruction-set generation and binary encoding.
 *
 * These are three different questions and the driver has historically only
 * asked the first one, because every part it supported answered the other two
 * the same way. That stops being true at M3:
 *
 *   - G13 (M1) and G14 (M2) are the AGX2 instruction set. That is what
 *     agx_pack.c emits and what src/asahi/isa/AGX2.xml describes.
 *   - G15 (M3), G16 (M4) and G17 (M5) are AGX3, a different instruction set.
 *     Apple's own compiler routes them through a separate LLVM target, and
 *     the community AGX2 decoder consumes 0% of their code.
 *   - AGX3 is not one encoding. Two binary encodings exist, and which one a
 *     part uses is now known for every chip this driver can enumerate.
 *
 * Nothing here can make the driver run on an AGX3 part. AGX3 can now be
 * *decoded* -- src/asahi/isa/AGX3{b,c}.xml generate working disassemblers --
 * but decoding is not encoding, and agx_pack.c still emits AGX2 only. What this
 * does is make the distinction explicit, so that a part the driver cannot
 * generate code for is refused by name instead of being handed AGX2
 * instructions, and so that each piece of the reverse engineering has somewhere
 * to land as it arrives.
 *
 * Evidence for every claim below is in research/gpu/, principally
 * GPU_AGX3_APPLE_ENCODER_TABLES_2026-08-23.md (encodings, Apple's tables),
 * GPU_DRIVER_CONTRACT_G15_G17_2026-08-23.md (the kernel core-name and chip-ID
 * decode that makes the mapping below well-founded) and
 * GPU_G15_G16_ISA_GENERATION_2026-08-23.md.
 */

enum agx_chip {
   /* AGX2. `G` is a single-cluster part, `X` is multi-cluster. */
   AGX_CHIP_G13G,
   AGX_CHIP_G13X,
   AGX_CHIP_G14G,
   AGX_CHIP_G14X,

   /* G15P is the A16 phone part and still executes AGX2. The Mac G15 parts
    * execute AGX3b; G15G additionally needs explicit scheduling nops. */
   AGX_CHIP_G15P,
   AGX_CHIP_G15G,
   AGX_CHIP_G15S,
   AGX_CHIP_G15C,
   AGX_CHIP_G15D,

   /* Every G16 part executes AGX3b. */
   AGX_CHIP_G16P,
   AGX_CHIP_G16G,
   AGX_CHIP_G16S,
   AGX_CHIP_G16C,

   /* G17P/G17A remain on the HAL200/AGX3b side of the boundary. The M5
    * G/S/C parts use AGX3c. */
   AGX_CHIP_G17P,
   AGX_CHIP_G17A,
   AGX_CHIP_G17G,
   AGX_CHIP_G17S,
   AGX_CHIP_G17C,

   AGX_CHIP_UNKNOWN,
};

/* Instruction set. Apple's compiler names these agx2 and agx3, and the low
 * nibble of an applegpu Mach-O cpusubtype is exactly this value. */
enum agx_isa {
   AGX_ISA_AGX2 = 2,
   AGX_ISA_AGX3 = 3,
   AGX_ISA_UNKNOWN = 0,
};

/*
 * Binary encoding.
 *
 * One instruction set, two encodings. Established by decoding a 274-kernel
 * corpus compiled for all ten AGX3 families with each of Apple's own encoder
 * tables, recovered from libapplegpu-nt.dylib. Every family decodes 100% under
 * exactly one table and 0-1.8% under any other -- no intermediate values:
 *
 *   agx3b (Apple variant 0): g15g, g15s, g15d, g16g, g16p, g16s, g17p
 *   agx3c (Apple variant 1): g17g, g17s, g18p
 *
 * An earlier partition by byte-identity read this as three groups, splitting
 * g15g off from the rest of variant 0. That was an artefact: g15g uses the same
 * encoder table, and differs only in that its scheduler pads with nops. See
 * agx_chip_needs_scheduling_stalls.
 *
 * The names match src/asahi/isa/AGX3b.xml and AGX3c.xml and the disassembler
 * entry points generated from them.
 */
enum agx_isa_encoding {
   AGX_ISA_ENCODING_AGX2,
   AGX_ISA_ENCODING_AGX3B,
   AGX_ISA_ENCODING_AGX3C,
   AGX_ISA_ENCODING_UNKNOWN,
};
#ifndef __OPENCL_VERSION__

#include <stdbool.h>
#include <stdint.h>

#include "drm-uapi/asahi_drm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return the exact supported command-stream architecture, or UNKNOWN.
 *
 * A recognised chip is not a supported one: G15 and above are recognised so
 * they can be reported by name, but have no code generator. Callers opening a
 * device must gate on agx_chip_codegen_supported, not on UNKNOWN. */
enum agx_chip
agx_chip_from_params(const struct drm_asahi_params_global *params);

/* The instruction set a chip executes. */
enum agx_isa agx_chip_isa(enum agx_chip chip);

/* The binary encoding of that instruction set. Known for every chip in the
 * enum; UNKNOWN only for AGX_CHIP_UNKNOWN. */
enum agx_isa_encoding agx_chip_isa_encoding(enum agx_chip chip);

/*
 * Whether this driver can generate code for a chip.
 *
 * False does not mean the hardware is incapable; it means there is no packing
 * table for its encoding. This is the gate a device open must use.
 */
bool agx_chip_codegen_supported(enum agx_chip chip);

/*
 * Hardware ray tracing.
 *
 * Present on every AGX3 part and on none before. Measured rather than
 * assumed: an intersect() call compiles to roughly 900-980 bytes on every
 * AGX3 family against 3315 bytes of software traversal on G13, G14 and the
 * one AGX2 G15 phone part.
 */
bool agx_chip_has_hardware_ray_tracing(enum agx_chip chip);

/*
 * Whether the compiler must insert explicit scheduling stalls.
 *
 * The M3 base part needs them and nothing else does: stripping its stall words
 * leaves an instruction stream 99.5% identical in count and 85.1%
 * byte-identical to M4's, with a further 7.4% differing only by branch
 * displacements shifted by exactly the removed padding.
 *
 * This is a property of one chip's scheduler, not of its encoding -- G15G
 * shares the agx3b encoder table with every other variant-0 part. It is a
 * codegen obligation, so it is only meaningful for a chip this driver can
 * generate code for.
 */
bool agx_chip_needs_scheduling_stalls(enum agx_chip chip);

/* Stable name for diagnostics, e.g. "G15X". Never NULL. */
const char *agx_chip_name(enum agx_chip chip);

/* Stable name for the instruction set, e.g. "AGX3". Never NULL. */
const char *agx_isa_name(enum agx_isa isa);

/* Stable name for the encoding, matching its XML table, e.g. "agx3b". Never
 * NULL. */
const char *agx_isa_encoding_name(enum agx_isa_encoding encoding);

#ifdef __cplusplus
}
#endif

#endif
