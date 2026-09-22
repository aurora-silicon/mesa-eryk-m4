/* Offline ownership stress: real Zink descriptor cache code, stub Vulkan calls.
 * No GPU. Compile with the host Mesa flags and ASan; see the companion runner.
 */
#include "zink_descriptors.c"
#include <pthread.h>
#include <stdatomic.h>

enum zink_descriptor_mode zink_descriptor_mode = ZINK_DESCRIPTOR_MODE_LAZY;
static pthread_barrier_t start;
static struct zink_screen *test_screen;
static atomic_uint next_handle = 1;
static VkResult VKAPI_CALL
create_stub(VkDevice dev, const VkDescriptorSetLayoutCreateInfo *info,
            const VkAllocationCallbacks *alloc, VkDescriptorSetLayout *layout)
{
   *layout = (VkDescriptorSetLayout)(uintptr_t)atomic_fetch_add(&next_handle, 1);
   return VK_SUCCESS;
}
static void VKAPI_CALL
destroy_stub(VkDevice dev, VkDescriptorSetLayout layout,
             const VkAllocationCallbacks *alloc) {}
static void *worker(void *arg)
{
   pthread_barrier_wait(&start);
   for (unsigned i = 0; i < 1000; i++) {
      enum zink_descriptor_type type = i % 2 ? ZINK_DESCRIPTOR_TYPE_UNIFORMS : ZINK_DESCRIPTOR_TYPE_UBO;
      VkDescriptorSetLayoutBinding binding = {.binding = i, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_VERTEX_BIT};
      struct zink_descriptor_layout_key *key;
      struct zink_descriptor_layout *layout = i % 3 ?
         descriptor_util_layout_get(test_screen, type, &binding, 1, &key) :
         create_layout(test_screen, type, &binding, 1, &key);
      assert(layout && key->num_bindings == 1 && key->bindings[0].binding == i);
      VkDescriptorPoolSize size = {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1};
      struct zink_descriptor_pool_key *pool = descriptor_util_pool_key_get(test_screen, type, key, &size, 1);
      assert(pool && pool->layout == key && pool->num_type_sizes == 1);
   }
   return NULL;
}
int main(void)
{
   test_screen = rzalloc(NULL, struct zink_screen);
   assert(test_screen);
   test_screen->vk.CreateDescriptorSetLayout = create_stub;
   test_screen->vk.DestroyDescriptorSetLayout = destroy_stub;
   assert(zink_descriptor_layouts_init(test_screen));
   pthread_t threads[8];
   assert(!pthread_barrier_init(&start, NULL, ARRAY_SIZE(threads)));
   for (unsigned i = 0; i < ARRAY_SIZE(threads); i++) assert(!pthread_create(&threads[i], NULL, worker, NULL));
   for (unsigned i = 0; i < ARRAY_SIZE(threads); i++) assert(!pthread_join(threads[i], NULL));
   zink_descriptor_layouts_deinit(test_screen);
   ralloc_free(test_screen);
   pthread_barrier_destroy(&start);
   puts("Zink descriptor allocation stress passed");
   return 0;
}
