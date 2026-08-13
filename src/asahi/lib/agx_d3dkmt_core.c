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

int
agx_d3dkmt_submit_private_flags(uint32_t submit_flags,
                                uint32_t *private_flags)
{
   if (private_flags == NULL ||
       (submit_flags & ~AGX_D3DKMT_SUBMIT_WRITE_SET_COMPLETE) != 0)
      return AGX_D3DKMT_ERR_INVAL;

   *private_flags = AGXW_SUBMIT_READ_SET_COMPLETE |
      ((submit_flags & AGX_D3DKMT_SUBMIT_WRITE_SET_COMPLETE)
          ? AGXW_SUBMIT_WRITE_SET_COMPLETE
          : 0);
   return AGX_D3DKMT_OK;
}

int
agx_d3dkmt_bo_read_range(const struct agx_d3dkmt_policy *policy,
                         bool low_va, uint64_t allocated_va,
                         uint64_t length,
                         struct agxw_submit_read_range *range)
{
   if (policy == NULL || range == NULL ||
       !agxw_va_range_is_user(allocated_va, length))
      return AGX_D3DKMT_ERR_INVAL;

   if (low_va) {
      if (policy->usc_size == 0 ||
          allocated_va < policy->usc_base ||
          allocated_va - policy->usc_base > policy->usc_size ||
          length > policy->usc_size -
                      (allocated_va - policy->usc_base))
         return AGX_D3DKMT_ERR_INVAL;
   }

   *range = (struct agxw_submit_read_range){
      .gpu_va = allocated_va,
      .length = length,
   };
   return AGX_D3DKMT_OK;
}

int
agx_d3dkmt_read_range_add(struct agxw_submit_read_range *ranges,
                          uint32_t *count, uint32_t capacity,
                          uint64_t gpu_va, uint64_t length)
{
   uint64_t new_end;
   uint32_t first = 0;

   if (ranges == NULL || count == NULL || *count > capacity || length == 0 ||
       gpu_va > UINT64_MAX - length ||
       !agxw_va_range_is_user(gpu_va, length))
      return AGX_D3DKMT_ERR_INVAL;
   new_end = gpu_va + length;

   while (first < *count) {
      uint64_t end = ranges[first].gpu_va + ranges[first].length;
      if (end >= gpu_va)
         break;
      first++;
   }

   if (first == *count || new_end < ranges[first].gpu_va) {
      if (*count == capacity)
         return AGX_D3DKMT_ERR_NOSPACE;
      memmove(&ranges[first + 1], &ranges[first],
              (size_t)(*count - first) * sizeof(*ranges));
      ranges[first] = (struct agxw_submit_read_range){gpu_va, length};
      (*count)++;
      return AGX_D3DKMT_OK;
   }

   uint64_t merged_start =
      gpu_va < ranges[first].gpu_va ? gpu_va : ranges[first].gpu_va;
   uint64_t first_end = ranges[first].gpu_va + ranges[first].length;
   uint64_t merged_end = new_end > first_end ? new_end : first_end;
   uint32_t last = first + 1;

   while (last < *count && ranges[last].gpu_va <= merged_end) {
      uint64_t end = ranges[last].gpu_va + ranges[last].length;
      if (end > merged_end)
         merged_end = end;
      last++;
   }

   ranges[first] = (struct agxw_submit_read_range){
      .gpu_va = merged_start,
      .length = merged_end - merged_start,
   };
   if (last > first + 1) {
      memmove(&ranges[first + 1], &ranges[last],
              (size_t)(*count - last) * sizeof(*ranges));
      *count -= last - (first + 1);
   }
   return AGX_D3DKMT_OK;
}

int
agx_d3dkmt_read_set_add_exact(
   struct agxw_submit_read_range *ranges, uint32_t *count,
   uint32_t capacity, const struct agx_submit_read_range *exact,
   uint32_t exact_count)
{
   if (ranges == NULL || count == NULL || *count > capacity ||
       exact_count > capacity || (exact_count != 0 && exact == NULL))
      return AGX_D3DKMT_ERR_INVAL;

   for (uint32_t i = 0; i < exact_count; ++i) {
      int ret = agx_d3dkmt_read_range_add(
         ranges, count, capacity, exact[i].gpu_va, exact[i].length);
      if (ret != AGX_D3DKMT_OK)
         return ret;
   }

   return AGX_D3DKMT_OK;
}

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
   free(pool->records);
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

   struct agx_d3dkmt_allocation_record *records =
      calloc(cap, sizeof(*records));
   uint32_t *fl = malloc((size_t)cap * sizeof(*fl));
   if (!records || !fl) {
      free(records);
      free(fl);
      return false;
   }

   if (pool->capacity > 0) {
      memcpy(records, pool->records,
             (size_t)pool->capacity * sizeof(*records));
      memcpy(fl, pool->freelist,
             (size_t)pool->capacity * sizeof(*fl));
   }

   free(pool->records);
   free(pool->freelist);
   pool->records = records;
   pool->freelist = fl;

   pool->capacity = cap;
   return true;
}

uint32_t
agx_d3dkmt_pool_alloc(struct agx_d3dkmt_handle_pool *pool, agx_kmt_handle kmt)
{
   const struct agx_d3dkmt_allocation_record record = {
      .allocation = kmt,
   };
   return agx_d3dkmt_pool_alloc_record(pool, &record);
}

uint32_t
agx_d3dkmt_pool_alloc_record(
   struct agx_d3dkmt_handle_pool *pool,
   const struct agx_d3dkmt_allocation_record *record)
{
   if (record == NULL || record->allocation == 0)
      return 0;

   if (record->external_type != AGX_D3DKMT_EXTERNAL_MEMORY_NONE &&
       record->resource == 0)
      return 0;
   if (record->runtime_resource != 0 &&
       (record->resource == 0 ||
        record->external_type != AGX_D3DKMT_EXTERNAL_MEMORY_NONE))
      return 0;
   if (record->external_type == AGX_D3DKMT_EXTERNAL_MEMORY_OPAQUE_WIN32_KMT &&
       record->global_share == 0)
      return 0;
   if (record->external_type !=
          AGX_D3DKMT_EXTERNAL_MEMORY_OPAQUE_WIN32_KMT &&
       record->global_share != 0)
      return 0;

   if (pool->nfree > 0) {
      uint32_t dense = pool->freelist[--pool->nfree];
      pool->records[dense] = *record;
      return dense;
   }

   if (pool->next == UINT32_MAX)
      return 0;
   if (!pool_grow(pool, pool->next + 1))
      return 0;

   uint32_t dense = pool->next++;
   pool->records[dense] = *record;
   return dense;
}

agx_kmt_handle
agx_d3dkmt_pool_get(const struct agx_d3dkmt_handle_pool *pool, uint32_t dense)
{
   if (dense == 0 || dense >= pool->capacity)
      return 0;
   return pool->records[dense].allocation;
}

bool
agx_d3dkmt_pool_get_record(const struct agx_d3dkmt_handle_pool *pool,
                           uint32_t dense,
                           struct agx_d3dkmt_allocation_record *record)
{
   if (record == NULL || dense == 0 || dense >= pool->capacity ||
       pool->records[dense].allocation == 0)
      return false;

   *record = pool->records[dense];
   return true;
}

bool
agx_d3dkmt_pool_rotate_runtime_identities(
   struct agx_d3dkmt_handle_pool *pool, const uint32_t *dense_handles,
   uint32_t count)
{
   if (!pool || (count && !dense_handles))
      return false;
   if (count < 2)
      return true;

   /* Validate the complete set before writing a byte.  In particular, an
    * external-memory record has different lifetime semantics and must never
    * be folded into DXGI's runtime-resource rotation. */
   for (uint32_t i = 0; i < count; ++i) {
      const uint32_t dense = dense_handles[i];
      if (!dense || dense >= pool->capacity ||
          !pool->records[dense].allocation ||
          !pool->records[dense].resource ||
          !pool->records[dense].runtime_resource ||
          pool->records[dense].external_type !=
             AGX_D3DKMT_EXTERNAL_MEMORY_NONE)
         return false;
      for (uint32_t j = 0; j < i; ++j) {
         if (dense_handles[j] == dense)
            return false;
      }
   }

   const struct agx_d3dkmt_allocation_record first =
      pool->records[dense_handles[0]];
   for (uint32_t i = 0; i + 1 < count; ++i) {
      struct agx_d3dkmt_allocation_record *destination =
         &pool->records[dense_handles[i]];
      const struct agx_d3dkmt_allocation_record *source =
         &pool->records[dense_handles[i + 1]];
      const uintptr_t runtime_resource = destination->runtime_resource;
      *destination = *source;
      destination->runtime_resource = runtime_resource;
   }

   struct agx_d3dkmt_allocation_record *last =
      &pool->records[dense_handles[count - 1]];
   const uintptr_t runtime_resource = last->runtime_resource;
   *last = first;
   last->runtime_resource = runtime_resource;
   return true;
}

agx_kmt_handle
agx_d3dkmt_pool_release(struct agx_d3dkmt_handle_pool *pool, uint32_t dense)
{
   struct agx_d3dkmt_allocation_record record;
   if (!agx_d3dkmt_pool_release_record(pool, dense, &record))
      return 0;
   return record.allocation;
}

bool
agx_d3dkmt_pool_release_record(
   struct agx_d3dkmt_handle_pool *pool, uint32_t dense,
   struct agx_d3dkmt_allocation_record *record)
{
   if (record == NULL || dense == 0 || dense >= pool->capacity ||
       pool->records[dense].allocation == 0)
      return false;

   *record = pool->records[dense];
   memset(&pool->records[dense], 0, sizeof(pool->records[dense]));
   /* freelist is sized with records, so nfree can never exceed capacity. */
   pool->freelist[pool->nfree++] = dense;
   return true;
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
   const uint64_t gran = AGX_D3DKMT_RESERVATION_GRANULARITY;
   const uint64_t mask = gran - 1;
   const uint64_t cb = AGXW_VM_KERNEL_CARVEOUT_BASE & ~mask;
   const uint64_t ce =
      (AGXW_VM_KERNEL_CARVEOUT_BASE + AGXW_VM_KERNEL_CARVEOUT_SIZE + mask) &
      ~mask;
   uint32_t n = 0;

   if (size == 0 || base > UINT64_MAX - size || base > UINT64_MAX - mask)
      return 0;

   uint64_t end = base + size;
   base = (base + mask) & ~mask;
   end &= ~mask;
   if (base >= end)
      return 0;

   /* Entirely below or entirely above: one inward-aligned range. */
   if (end <= cb || base >= ce) {
      out[0].base = base;
      out[0].size = end - base;
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

static bool
va_bitmap_range(uint64_t base, uint64_t size, uint64_t addr,
                uint64_t range_size, uint64_t *first_page, uint64_t *page_count)
{
   const uint64_t page = AGX_D3DKMT_PAGE_SIZE;

   if (size == 0 || range_size == 0 || (base % page) != 0 ||
       (size % page) != 0 || (addr % page) != 0 || (range_size % page) != 0 ||
       base > UINT64_MAX - size || addr < base ||
       addr > UINT64_MAX - range_size || addr + range_size > base + size)
      return false;

   *first_page = (addr - base) / page;
   *page_count = range_size / page;
   return true;
}

uint32_t
agx_d3dkmt_va_bitmap_word_count(uint64_t base, uint64_t size)
{
   const uint64_t page = AGX_D3DKMT_PAGE_SIZE;
   if (size == 0 || (base % page) != 0 || (size % page) != 0 ||
       base > UINT64_MAX - size)
      return 0;

   uint64_t pages = size / page;
   uint64_t words = (pages + 63) / 64;
   return words <= UINT32_MAX ? (uint32_t)words : 0;
}

static bool
va_bitmap_find_highest_set(const uint64_t *words, uint64_t first,
                           uint64_t count, uint64_t *highest)
{
   uint64_t last = first + count - 1;
   uint64_t first_word = first / 64;
   uint64_t last_word = last / 64;

   for (uint64_t w = last_word;; --w) {
      unsigned lo = w == first_word ? (unsigned)(first % 64) : 0;
      unsigned hi = w == last_word ? (unsigned)(last % 64) : 63;
      uint64_t lo_mask = UINT64_MAX << lo;
      uint64_t hi_mask =
         hi == 63 ? UINT64_MAX : ((UINT64_C(1) << (hi + 1)) - 1);
      uint64_t used = words[w] & lo_mask & hi_mask;
      if (used != 0) {
         unsigned bit = 63u;
         while (((used >> bit) & 1u) == 0)
            bit--;
         *highest = w * 64 + bit;
         return true;
      }

      if (w == first_word)
         break;
   }

   return false;
}

static void
va_bitmap_set(uint64_t *words, uint64_t first, uint64_t count, bool used)
{
   for (uint64_t page = first; page < first + count; ++page) {
      uint64_t mask = UINT64_C(1) << (page % 64);
      if (used)
         words[page / 64] |= mask;
      else
         words[page / 64] &= ~mask;
   }
}

uint64_t
agx_d3dkmt_va_bitmap_alloc(uint64_t *words, uint32_t word_count, uint64_t base,
                           uint64_t size, uint64_t allowed_base,
                           uint64_t allowed_size, uint64_t request_size,
                           uint64_t alignment, uint64_t fixed_va)
{
   const uint64_t page = AGX_D3DKMT_PAGE_SIZE;
   uint64_t allowed_first, allowed_pages;

   if (words == NULL || word_count == 0 || alignment < page ||
       (alignment % page) != 0 || (alignment & (alignment - 1)) != 0 ||
       !va_bitmap_range(base, size, allowed_base, allowed_size, &allowed_first,
                        &allowed_pages) ||
       request_size == 0 || request_size > allowed_size ||
       (request_size % page) != 0 || (uint64_t)word_count * 64 < size / page)
      return 0;

   uint64_t request_pages = request_size / page;
   uint64_t candidate;
   if (fixed_va != 0) {
      if ((fixed_va % alignment) != 0 || fixed_va < allowed_base ||
          fixed_va > UINT64_MAX - request_size ||
          fixed_va + request_size > allowed_base + allowed_size)
         return 0;
      candidate = fixed_va;
   } else {
      candidate =
         (allowed_base + allowed_size - request_size) & ~(alignment - 1);
   }

   for (;;) {
      if (candidate < allowed_base)
         return 0;

      uint64_t first, pages;
      if (!va_bitmap_range(base, size, candidate, request_size, &first,
                           &pages) ||
          pages != request_pages)
         return 0;

      uint64_t highest;
      if (!va_bitmap_find_highest_set(words, first, pages, &highest)) {
         va_bitmap_set(words, first, pages, true);
         return candidate;
      }

      if (fixed_va != 0)
         return 0;

      uint64_t occupied = base + highest * page;
      if (occupied < allowed_base + request_size)
         return 0;
      candidate = (occupied - request_size) & ~(alignment - 1);
   }
}

bool
agx_d3dkmt_va_bitmap_free(uint64_t *words, uint32_t word_count, uint64_t base,
                          uint64_t size, uint64_t addr,
                          uint64_t allocation_size)
{
   uint64_t first, pages;
   if (words == NULL || word_count == 0 ||
       (uint64_t)word_count * 64 < size / AGX_D3DKMT_PAGE_SIZE ||
       !va_bitmap_range(base, size, addr, allocation_size, &first, &pages))
      return false;

   for (uint64_t page = first; page < first + pages; ++page) {
      if ((words[page / 64] & (UINT64_C(1) << (page % 64))) == 0)
         return false;
   }

   va_bitmap_set(words, first, pages, false);
   return true;
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

   if (agx_bo_flags & AGX_D3DKMT_BO_PRIMARY)
      return AGXW_ALLOC_SHAREABLE | AGXW_ALLOC_PRIMARY;

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
agx_d3dkmt_cpu_visible_flags(uint32_t agx_bo_flags)
{
   if (agx_bo_flags & AGX_D3DKMT_BO_EXEC)
      agx_bo_flags |= AGX_D3DKMT_BO_WRITEBACK;

   return agx_bo_flags;
}

uint32_t
agx_d3dkmt_create_flags(uint32_t agx_bo_flags)
{
   enum agx_d3dkmt_external_memory_type type =
      (agx_bo_flags & (AGX_D3DKMT_BO_SHARED | AGX_D3DKMT_BO_SHAREABLE))
         ? AGX_D3DKMT_EXTERNAL_MEMORY_OPAQUE_WIN32
         : AGX_D3DKMT_EXTERNAL_MEMORY_NONE;
   return agx_d3dkmt_create_flags_external(agx_bo_flags, type);
}

uint32_t
agx_d3dkmt_create_flags_external(
   uint32_t agx_bo_flags,
   enum agx_d3dkmt_external_memory_type external_type)
{
   if (external_type > AGX_D3DKMT_EXTERNAL_MEMORY_OPAQUE_WIN32_KMT)
      return 0;
   if (external_type != AGX_D3DKMT_EXTERNAL_MEMORY_NONE &&
       !(agx_bo_flags &
         (AGX_D3DKMT_BO_SHARED | AGX_D3DKMT_BO_SHAREABLE)))
      return 0;

   uint32_t flags = 0;

   if (external_type != AGX_D3DKMT_EXTERNAL_MEMORY_NONE) {
      /* A global share handle always names a RESOURCE, never a bare
       * allocation. One allocation per resource is the miniport contract. */
      flags |= AGX_KMT_CREATEALLOC_CREATERESOURCE |
               AGX_KMT_CREATEALLOC_CREATESHARED;

      if (external_type == AGX_D3DKMT_EXTERNAL_MEMORY_OPAQUE_WIN32)
         flags |= AGX_KMT_CREATEALLOC_NTSECURITYSHARING;
   }

   /* Honeykrisp memsets or fully overwrites everything it allocates, and
    * zeroing a fresh BO is pure cost on a UMA part. */
   flags |= AGX_KMT_CREATEALLOC_ALLOWNOTZEROED;

   /* NEVER ExistingSysMem: CONTRACT.md SS3.6 rejects it with
    * STATUS_INVALID_PARAMETER because AGX BOs must be 16 KiB
    * physically-chunked and arbitrary user pages are not. */
   return flags;
}

uint32_t
agx_d3dkmt_create_flags_runtime_resource(uint32_t agx_bo_flags)
{
   /*
    * pfnAllocateCb receives hResource separately from dxgkrnl.  The public
    * D3DKMT CreateAllocation2 CreateResource/CreateShared flags would ask it
    * to create a second resource and are consequently illegal in this path.
    * Keep the zeroing hint; allocation category/shareability is carried by
    * the pinned agxw_alloc_private blob, not these transport flags.
    */
   (void)agx_bo_flags;
   return AGX_KMT_CREATEALLOC_ALLOWNOTZEROED;
}
