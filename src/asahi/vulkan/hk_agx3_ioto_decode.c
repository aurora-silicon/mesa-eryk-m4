/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 */

#include "hk_agx3_ioto_decode.h"

bool
hk_agx3_ioto_decode_record(enum agx_chip chip, const void *stream,
                           size_t available, struct agx_ioto_record *record,
                           size_t *consumed)
{
   return agx_ioto_decode_one(chip, stream, available, record, consumed);
}

bool
hk_agx3_ioto_validate_record(enum agx_chip chip,
                             enum agx_ioto_kind expected,
                             const void *stream, size_t available,
                             struct agx_ioto_record *record,
                             size_t *consumed)
{
   return agx_ioto_validate_kind(chip, expected, stream, available, record,
                                 consumed);
}
