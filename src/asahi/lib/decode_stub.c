/*
 * Copyright 2026 the Asahi Linux contributors
 * SPDX-License-Identifier: MIT
 */

/* The command stream decoder depends on POSIX memory mapping.  Windows
 * drivers retain the debug hooks, but tracing is intentionally a no-op until
 * a platform decoder is available. */

#include <stdint.h>

#include "decode.h"

struct agxdecode_ctx {
   int unused;
};

static struct agxdecode_ctx stub_ctx;

struct agxdecode_ctx *
agxdecode_new_context(uint64_t shader_base)
{
   return &stub_ctx;
}

void
agxdecode_destroy_context(struct agxdecode_ctx *ctx)
{
}

void
agxdecode_next_frame(void)
{
}

void
agxdecode_track_alloc(struct agxdecode_ctx *ctx, struct agx_bo *alloc)
{
}

void
agxdecode_track_free(struct agxdecode_ctx *ctx, struct agx_bo *bo)
{
}

void
agxdecode_drm_cmdbuf(struct agxdecode_ctx *ctx,
                     struct drm_asahi_params_global *params,
                     struct util_dynarray *cmdbuf, bool verbose)
{
}
