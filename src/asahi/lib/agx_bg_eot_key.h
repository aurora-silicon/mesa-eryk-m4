/* SPDX-License-Identifier: MIT */
#pragma once
#include "util/format/u_formats.h"
#include <stdint.h>
enum pipe_format agx_g17p_rt0_pbe_format(enum pipe_format format);
uint8_t agx_g17p_mrt_eot_selector(enum pipe_format format);
