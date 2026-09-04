/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* The legacy AGX2 libagx printf/abort buffer address is fixed at compile time.
 * AGX3 frontends publish a process-local address through runtime root state.
 */
#define LIBAGX_PRINTF_BUFFER_ADDRESS (1ull << 36)
#define LIBAGX_PRINTF_BUFFER_SIZE    (16384)
