/*
 * Copyright 2026 the Asahi Linux contributors
 * SPDX-License-Identifier: MIT
 */

/*
 * Portable half of the D3DKMT winsys -- see agx_d3dkmt_core.h for why this is
 * a separate, Mesa-free, Windows-free translation unit.
 */

#include "agx_d3dkmt_core.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Handle pool.
 * ------------------------------------------------------------------------- */
void
agx_d3dkmt_pool_init(struct agx_d3dkmt_handle_pool *pool)
{
   memset(pool, 0, sizeof(*pool));
   /* Dense handle 0 is reserved: agx_bo_bind() passes handle 0 to mean
    * "no BO" (agx_device.c:139), so it must never name a live allocation. */
   pool->next = 1;
}

void
agx_d3dkmt_pool_finish(struct agx_d3dkmt_handle_pool *pool)
{
   free(pool->kmt);
   free(pool->freelist);
   memset(pool, 0, sizeof(*pool));
}

static bool
pool_grow(struct agx_d3dkmt_handle_pool *pool, uint32_t need)
{
   if (need <= pool->capacity)
      return true;

   uint32_t cap = pool->capacity ? pool->capacity : 64;
   while (cap < need) {
      if (cap > UINT32_MAX / 2)
         return false;
      cap *= 2;
   }

   agx_kmt_handle *kmt = realloc(pool->kmt, (size_t)cap * sizeof(*kmt));
   if (!kmt)
      return false;
   pool->kmt = kmt;
   memset(pool->kmt + pool->capacity, 0,
          (size_t)(cap - pool->capacity) * sizeof(*kmt));

   uint32_t *fl = realloc(pool->freelist, (size_t)cap * sizeof(*fl));
   if (!fl)
      return false;
   pool->freelist = fl;

   pool->capacity = cap;
   return true;
}

uint32_t
agx_d3dkmt_pool_alloc(struct agx_d3dkmt_handle_pool *pool, agx_kmt_handle kmt)
{
   if (kmt == 0)
      return 0;

   if (pool->nfree > 0) {
      uint32_t dense = pool->freelist[--pool->nfree];
      pool->kmt[dense] = kmt;
      return dense;
   }

   if (pool->next == UINT32_MAX)
      return 0;
   if (!pool_grow(pool, pool->next + 1))
      return 0;

   uint32_t dense = pool->next++;
   pool->kmt[dense] = kmt;
   return dense;
}

agx_kmt_handle
agx_d3dkmt_pool_get(const struct agx_d3dkmt_handle_pool *pool, uint32_t dense)
{
   if (dense == 0 || dense >= pool->capacity)
      return 0;
   return pool->kmt[dense];
}

agx_kmt_handle
agx_d3dkmt_pool_release(struct agx_d3dkmt_handle_pool *pool, uint32_t dense)
{
   if (dense == 0 || dense >= pool->capacity)
      return 0;

   agx_kmt_handle kmt = pool->kmt[dense];
   if (kmt == 0)
      return 0;

   pool->kmt[dense] = 0;
   /* freelist is sized with kmt, so nfree can never exceed capacity. */
   pool->freelist[pool->nfree++] = dense;
   return kmt;
}

/* -------------------------------------------------------------------------
 * VM_BIND translation.
 * ------------------------------------------------------------------------- */
static bool
in_usc(const struct agx_d3dkmt_policy *p, uint64_t addr, uint64_t range)
{
   if (p->usc_size == 0)
      return false;

   return addr >= p->usc_base && (addr - p->usc_base) < p->usc_size &&
          range <= p->usc_size - (addr - p->usc_base);
}

static int
translate_one(const struct agx_d3dkmt_bind *b,
              const struct agx_d3dkmt_policy *policy,
              struct agx_kmt_updategpuva_op *op)
{
   /*
    * Contract rule G1 (CONTRACT.md SS4.6): every base, offset and size is a
    * multiple of 16 KiB. Validated with the miniport's own predicate rather
    * than a second copy of the rule, so the two sides can never disagree
    * about what is legal. agx_bo_bind() asserts the same thing upstream
    * (agx_device.c:134-136), but an assert compiles out in release and a
    * misaligned SizeInBytes silently corrupts a page-table update.
    */
   if (!agxw_va_range_is_bindable(b->addr, b->range) ||
       (b->offset % AGXW_PAGE_SIZE) != 0)
      return AGX_D3DKMT_ERR_INVAL;

   /* CONTRACT.md SS4.2: the user window is NOT contiguous. This rejects a
    * bind that would straddle or land on the kernel carve-out page. */
   if (!agxw_va_range_is_user(b->addr, b->range))
      return AGX_D3DKMT_ERR_INVAL;

   memset(op, 0, sizeof(*op));

   if (b->flags & AGX_D3DKMT_BIND_UNBIND) {
      op->OperationType = AGX_KMT_UPDATEGPUVA_UNMAP;
      op->Unmap.BaseAddress = b->addr;
      op->Unmap.SizeInBytes = b->range;
      op->Unmap.Protection.Value = AGX_KMT_GPUVA_PROT_NOACCESS;
      return AGX_D3DKMT_OK;
   }

   if (b->kmt_handle == 0)
      return AGX_D3DKMT_ERR_INVAL;

   uint64_t prot = 0;
   uint64_t drv = 0;

   if (b->flags & AGX_D3DKMT_BIND_WRITE)
      prot |= AGX_KMT_GPUVA_PROT_WRITE;

   /* WDDM cannot encode write-without-read; AGX's AP bits can, so the
    * distinction rides in DriverProtection (CONTRACT.md SS4.7). */
   if ((b->flags & AGX_D3DKMT_BIND_WRITE) && !(b->flags & AGX_D3DKMT_BIND_READ))
      drv |= AGXW_VAPROT_WRITE_ONLY;

   /* A bind op carries no execute bit -- drm_asahi_gem_bind_op has no such
    * field -- so executability is derived from the address, exactly as the
    * Linux driver derives it from the VA region. */
   if (in_usc(policy, b->addr, b->range)) {
      prot |= AGX_KMT_GPUVA_PROT_EXECUTE;
      drv |= AGXW_VAPROT_USC;
   }

   /* One page aliased across the whole range, natively (CONTRACT.md SS4.7).
    * No expansion, no page-table doubling: one operation. */
   if (b->flags & AGX_D3DKMT_BIND_SINGLE_PAGE)
      drv |= AGXW_VAPROT_SINGLE_PAGE;

   op->OperationType = AGX_KMT_UPDATEGPUVA_MAP_PROTECT;
   op->MapProtect.BaseAddress = b->addr;
   op->MapProtect.SizeInBytes = b->range;
   op->MapProtect.hAllocation = b->kmt_handle;
   op->MapProtect.AllocationOffsetInBytes = b->offset;
   op->MapProtect.AllocationSizeInBytes =
      (b->flags & AGX_D3DKMT_BIND_SINGLE_PAGE) ? AGXW_PAGE_SIZE : b->range;
   op->MapProtect.Protection.Value = prot;
   op->MapProtect.DriverProtection = drv;

   return AGX_D3DKMT_OK;
}

int
agx_d3dkmt_translate_binds(const struct agx_d3dkmt_bind *binds, uint32_t count,
                           const struct agx_d3dkmt_policy *policy,
                           struct agx_kmt_updategpuva_op *out,
                           uint32_t out_capacity, uint32_t *out_count)
{
   if (out_count == NULL || policy == NULL || (count > 0 && binds == NULL))
      return AGX_D3DKMT_ERR_INVAL;

   *out_count = count;

   if (out == NULL)
      return AGX_D3DKMT_OK;

   if (count > out_capacity)
      return AGX_D3DKMT_ERR_NOSPACE;

   for (uint32_t i = 0; i < count; ++i) {
      int ret = translate_one(&binds[i], policy, &out[i]);
      if (ret != AGX_D3DKMT_OK) {
         *out_count = 0;
         return ret;
      }
   }

   return AGX_D3DKMT_OK;
}

uint32_t
agx_d3dkmt_va_split_carveout(uint64_t base, uint64_t size,
                             struct agx_d3dkmt_va_range out[2])
{
   const uint64_t cb = AGXW_VM_KERNEL_CARVEOUT_BASE;
   const uint64_t ce = AGXW_VM_KERNEL_CARVEOUT_BASE + AGXW_VM_KERNEL_CARVEOUT_SIZE;
   uint32_t n = 0;

   if (size == 0)
      return 0;

   uint64_t end = base + size;

   /* Entirely below or entirely above: one range, unchanged. */
   if (end <= cb || base >= ce) {
      out[0].base = base;
      out[0].size = size;
      return 1;
   }

   if (base < cb) {
      out[n].base = base;
      out[n].size = cb - base;
      n++;
   }

   if (end > ce) {
      out[n].base = ce;
      out[n].size = end - ce;
      n++;
   }

   return n;
}

/* -------------------------------------------------------------------------
 * Sync table.
 * ------------------------------------------------------------------------- */
void
agx_d3dkmt_sync_table_init(struct agx_d3dkmt_sync_table *t)
{
   memset(t, 0, sizeof(*t));
   /* Handle 0 is reserved: vk_drm_syncobj and hk_queue.c both treat a zero
    * syncobj handle as "none". */
   t->next = 1;
}

void
agx_d3dkmt_sync_table_finish(struct agx_d3dkmt_sync_table *t)
{
   free(t->slots);
   free(t->freelist);
   memset(t, 0, sizeof(*t));
}

static bool
sync_grow(struct agx_d3dkmt_sync_table *t, uint32_t need)
{
   if (need <= t->capacity)
      return true;

   uint32_t cap = t->capacity ? t->capacity : 32;
   while (cap < need) {
      if (cap > UINT32_MAX / 2)
         return false;
      cap *= 2;
   }

   struct agx_d3dkmt_sync *slots =
      realloc(t->slots, (size_t)cap * sizeof(*slots));
   if (!slots)
      return false;
   t->slots = slots;
   memset(t->slots + t->capacity, 0,
          (size_t)(cap - t->capacity) * sizeof(*slots));

   uint32_t *fl = realloc(t->freelist, (size_t)cap * sizeof(*fl));
   if (!fl)
      return false;
   t->freelist = fl;

   t->capacity = cap;
   return true;
}

uint32_t
agx_d3dkmt_sync_table_add(struct agx_d3dkmt_sync_table *t, agx_kmt_handle kmt,
                          const volatile uint64_t *cpu_va,
                          uint64_t initial_value)
{
   uint32_t handle;

   if (kmt == 0)
      return 0;

   if (t->nfree > 0) {
      handle = t->freelist[--t->nfree];
   } else {
      if (t->next == UINT32_MAX || !sync_grow(t, t->next + 1))
         return 0;
      handle = t->next++;
   }

   t->slots[handle] = (struct agx_d3dkmt_sync){
      .kmt = kmt,
      .base = 0,
      .last_request = initial_value,
      .cpu_va = cpu_va,
   };
   return handle;
}

struct agx_d3dkmt_sync *
agx_d3dkmt_sync_table_get(struct agx_d3dkmt_sync_table *t, uint32_t handle)
{
   if (handle == 0 || handle >= t->capacity || t->slots[handle].kmt == 0)
      return NULL;
   return &t->slots[handle];
}

agx_kmt_handle
agx_d3dkmt_sync_table_remove(struct agx_d3dkmt_sync_table *t, uint32_t handle)
{
   struct agx_d3dkmt_sync *s = agx_d3dkmt_sync_table_get(t, handle);
   if (s == NULL)
      return 0;

   agx_kmt_handle kmt = s->kmt;
   memset(s, 0, sizeof(*s));
   t->freelist[t->nfree++] = handle;
   return kmt;
}

uint64_t
agx_d3dkmt_sync_current(const struct agx_d3dkmt_sync *s)
{
   if (s->cpu_va != NULL) {
      uint64_t kernel = *s->cpu_va;
      /* The GPU can be ahead of anything we requested (another process's
       * shared fence), and we can be ahead of the GPU (a request not yet
       * retired). "Current" for signalling decisions is the larger. */
      return kernel > s->last_request ? kernel : s->last_request;
   }

   return s->last_request;
}

uint64_t
agx_d3dkmt_binary_signal_value(const struct agx_d3dkmt_sync *s, bool *skip)
{
   uint64_t target = s->base + 1;

   if (skip != NULL)
      *skip = agx_d3dkmt_sync_current(s) >= target;

   return target;
}

uint64_t
agx_d3dkmt_binary_wait_value(const struct agx_d3dkmt_sync *s)
{
   return s->base + 1;
}

uint64_t
agx_d3dkmt_binary_reset_base(const struct agx_d3dkmt_sync *s)
{
   return agx_d3dkmt_sync_current(s);
}

/* -------------------------------------------------------------------------
 * Timeouts.
 * ------------------------------------------------------------------------- */
uint32_t
agx_d3dkmt_timeout_to_ms(int64_t abs_timeout_nsec, int64_t now_nsec)
{
   if (abs_timeout_nsec == INT64_MAX)
      return AGX_D3DKMT_INFINITE;

   if (abs_timeout_nsec <= now_nsec)
      return 0;

   /* Both operands are non-negative-difference here, so this cannot
    * overflow: abs > now, and both are int64. */
   uint64_t delta_ns = (uint64_t)(abs_timeout_nsec - now_nsec);
   uint64_t ms = (delta_ns + 999999ull) / 1000000ull;

   if (ms >= AGX_D3DKMT_INFINITE)
      return AGX_D3DKMT_INFINITE - 1;

   return (uint32_t)ms;
}

uint32_t
agx_d3dkmt_first_signaled(const uint64_t *current, const uint64_t *wanted,
                          uint32_t count)
{
   for (uint32_t i = 0; i < count; ++i) {
      if (current[i] >= wanted[i])
         return i;
   }

   return UINT32_MAX;
}

/* -------------------------------------------------------------------------
 * Contract blob helpers.
 * ------------------------------------------------------------------------- */
int
agx_d3dkmt_parse_identity(const void *blob, uint32_t blob_size,
                          struct agx_d3dkmt_policy *policy,
                          struct agxw_adapter_identity *out)
{
   struct agxw_adapter_identity id;

   if (blob == NULL || policy == NULL || out == NULL)
      return AGX_D3DKMT_ERR_INVAL;

   /*
    * Width is asserted, never inferred. The D3D runtime does not validate
    * private-data sizes across the user/kernel boundary (CONTRACT.md SS12.1
    * makes the same argument for the allocation blob), so a short reply is a
    * different adapter, not a truncated ours.
    */
   if (blob_size < sizeof(id))
      return AGX_D3DKMT_ERR_INVAL;

   memcpy(&id, blob, sizeof(id));

   if (id.magic != AGXW_ADAPTER_IDENTITY_MAGIC)
      return AGX_D3DKMT_ERR_INVAL;
   if (id.version != AGXW_ADAPTER_IDENTITY_VERSION)
      return AGX_D3DKMT_ERR_INVAL;
   if (id.struct_size != sizeof(id))
      return AGX_D3DKMT_ERR_INVAL;
   if (id.contract_version != AGXW_CONTRACT_VERSION)
      return AGX_D3DKMT_ERR_INVAL;
   if (id.params_blob_size != AGXW_PARAMS_GLOBAL_SIZE)
      return AGX_D3DKMT_ERR_INVAL;

   *policy = agx_d3dkmt_policy_default();
   *out = id;
   return AGX_D3DKMT_OK;
}

uint32_t
agx_d3dkmt_agxw_alloc_flags(uint32_t agx_bo_flags)
{
   uint32_t flags = 0;

   if (agx_bo_flags & AGX_D3DKMT_BO_WRITEBACK)
      flags |= AGXW_ALLOC_WRITEBACK;

   /* Mutually exclusive by contract: an allocation is either private to this
    * device's VM or shareable, never both (agxw_contract.h:139-143). */
   if (agx_bo_flags & (AGX_D3DKMT_BO_SHARED | AGX_D3DKMT_BO_SHAREABLE))
      flags |= AGXW_ALLOC_SHAREABLE;
   else
      flags |= AGXW_ALLOC_VM_PRIVATE;

   return flags;
}

uint32_t
agx_d3dkmt_create_flags(uint32_t agx_bo_flags)
{
   uint32_t flags = 0;

   if (agx_bo_flags & (AGX_D3DKMT_BO_SHARED | AGX_D3DKMT_BO_SHAREABLE)) {
      /* NT-handle sharing rather than the legacy global shared handle: that
       * is what VK_KHR_external_memory_win32 and D3D11.1 shared NT handles
       * both require, and it is the reason this port exists -- one shared
       * handle namespace across Vulkan and D3D. */
      flags |= AGX_KMT_CREATEALLOC_CREATESHARED |
               AGX_KMT_CREATEALLOC_NTSECURITYSHARING;
   }

   /* Honeykrisp memsets or fully overwrites everything it allocates, and
    * zeroing a fresh BO is pure cost on a UMA part. */
   flags |= AGX_KMT_CREATEALLOC_ALLOWNOTZEROED;

   /* NEVER ExistingSysMem: CONTRACT.md SS3.6 rejects it with
    * STATUS_INVALID_PARAMETER because AGX BOs must be 16 KiB
    * physically-chunked and arbitrary user pages are not. */
   return flags;
}
