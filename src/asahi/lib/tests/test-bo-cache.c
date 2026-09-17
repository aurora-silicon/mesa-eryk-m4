/* SPDX-License-Identifier: MIT
 * Exercise the real internal cache on CPU, with synthetic BOs and no DRM.
 * Compiled by bo-cache-test.py using the checkout's host compilation flags.
 */
#include "agx_bo.c"

static unsigned freed;

void
agx_bo_free(struct agx_device *dev, struct agx_bo *bo)
{
   assert(bo->bucket_link.next == NULL);
   assert(bo->lru_link.next == NULL);
   freed++;
}

static struct agx_bo
make_bo(size_t size, unsigned flags, size_t align)
{
   return (struct agx_bo){.size = size, .flags = flags, .align = align,
                         .label = "cache test"};
}

int
main(void)
{
   struct agx_device *dev = calloc(1, sizeof(*dev));
   assert(dev);
   simple_mtx_init(&dev->bo_cache.lock, mtx_plain);
   list_inithead(&dev->bo_cache.lru);
   for (unsigned i = 0; i < ARRAY_SIZE(dev->bo_cache.buckets); i++)
      list_inithead(&dev->bo_cache.buckets[i]);

   /* Mixed flags, including hash collisions and unknown future flag bits.
    * Reverse retrieval must return the exact compatible object every time.
    */
   struct agx_bo mixed[128];
   for (unsigned i = 0; i < ARRAY_SIZE(mixed); i++) {
      mixed[i] = make_bo(0x4000, i << 1, 0x4000);
      assert(agx_bo_cache_put(dev, &mixed[i]));
   }
   for (unsigned i = ARRAY_SIZE(mixed); i-- > 0;) {
      assert(agx_bo_cache_fetch(dev, 0x4000, 0x4000, i << 1, true)
             == &mixed[i]);
   }
   assert(dev->bo_cache.size == 0 && list_is_empty(&dev->bo_cache.lru));

   /* Within one size class: insufficient size/alignment must be skipped;
    * FIFO order for compatible entries and the global LRU are preserved.
    */
   struct agx_bo small = make_bo(0x8000, 0, 0x4000);
   struct agx_bo unaligned = make_bo(0xc000, 0, 0x4000);
   struct agx_bo suitable = make_bo(0xc000, 0, 0x10000);
   assert(agx_bo_cache_put(dev, &small));
   assert(agx_bo_cache_put(dev, &unaligned));
   assert(agx_bo_cache_put(dev, &suitable));
   assert(agx_bo_cache_fetch(dev, 0xc000, 0x10000, 0, true) == &suitable);
   assert(agx_bo_cache_fetch(dev, 0xc000, 0x10000, 0, true) == NULL);
   assert(agx_bo_cache_trim(dev, false, false, 1) == small.size);
   assert(freed == 1);
   assert(agx_bo_cache_fetch(dev, 0xc000, 0x4000, 0, true) == &unaligned);
   assert(dev->bo_cache.size == 0);

   struct agx_bo shared = make_bo(0x4000, AGX_BO_SHARED, 0x4000);
   assert(!agx_bo_cache_put(dev, &shared));
   for (unsigned i = 0; i < ARRAY_SIZE(mixed); i++)
      assert(agx_bo_cache_put(dev, &mixed[i]));
   agx_bo_cache_evict_all(dev);
   assert(freed == ARRAY_SIZE(mixed) + 1);
   assert(dev->bo_cache.size == 0 && list_is_empty(&dev->bo_cache.lru));
   for (unsigned i = 0; i < ARRAY_SIZE(dev->bo_cache.buckets); i++)
      assert(list_is_empty(&dev->bo_cache.buckets[i]));
   /* An expired cohort must not all be freed by one latency-sensitive put.
    * Check exact age boundaries without sleeping or depending on wall time. */
   const int64_t age = INT64_C(2000000000);
   const int64_t now = os_time_get_nano();
   for (unsigned i = 0; i < 8; i++) {
      mixed[i] = make_bo(0x4000, 0, 0x4000);
      assert(agx_bo_cache_put(dev, &mixed[i]));
   }
   for (unsigned i = 0; i < 8; i++)
      mixed[i].last_used_ns = now - age;
   unsigned before = freed;
   simple_mtx_lock(&dev->bo_cache.lock);
   agx_bo_cache_evict_stale_bos(dev, now);
   assert(freed == before);
   agx_bo_cache_evict_stale_bos(dev, now + 1);
   assert(freed == before + 1 && dev->bo_cache.size == 7 * 0x4000);
   agx_bo_cache_evict_stale_bos(dev, now + 1);
   assert(freed == before + 2 && dev->bo_cache.size == 6 * 0x4000);
   simple_mtx_unlock(&dev->bo_cache.lock);
   /* Explicit pressure still frees the full requested amount immediately. */
   assert(agx_bo_cache_trim(dev, false, false, 3 * 0x4000) == 3 * 0x4000);
   assert(freed == before + 5);
   agx_bo_cache_evict_all(dev);
   assert(freed == before + 8 && dev->bo_cache.size == 0);
   struct agx_bo oversized = make_bo(AGX_BO_CACHE_MAX_SIZE + 0x4000, 0, 0x4000);
   assert(!agx_bo_cache_put(dev, &oversized));
   struct agx_bo full = make_bo(AGX_BO_CACHE_MAX_SIZE, 0, 0x4000);
   assert(agx_bo_cache_put(dev, &full));
   assert(!agx_bo_cache_put(dev, &mixed[0]));
   assert(dev->bo_cache.size == AGX_BO_CACHE_MAX_SIZE);
   agx_bo_cache_evict_all(dev);
   simple_mtx_destroy(&dev->bo_cache.lock);
   free(dev);
   puts("PASS: real BO cache flags, collisions, size, alignment, LRU and eviction");
   return 0;
}
