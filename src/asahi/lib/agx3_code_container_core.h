/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Write the 0x40-byte program immediately before a G17P container entry.
 * Apple's HAL200 compiler emits a SETPROFILECTL program for vertex/compute,
 * but a bare stop for fragment. */
#define AGX3_CONTAINER_CONSTANT_PROGRAM_SIZE 0x40u
void agx3_build_container_constant_program(
   uint8_t image[AGX3_CONTAINER_CONSTANT_PROGRAM_SIZE], bool fragment);
