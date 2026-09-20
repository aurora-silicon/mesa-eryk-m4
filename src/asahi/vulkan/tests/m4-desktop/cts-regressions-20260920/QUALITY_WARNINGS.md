# Quality warning review

Four allocation-callback warnings are retained as QualityWarning, not Pass:

- api.object_management.alloc_callback_fail.descriptor_set_layout_empty
- api.object_management.alloc_callback_fail.descriptor_set_layout_single
- api.object_management.alloc_callback_fail.pipeline_layout_empty
- api.object_management.alloc_callback_fail.pipeline_layout_single

Runtime descriptor/pipeline layouts are ref-counted and can outlive their public
handles because pipelines and descriptor sets retain them. The common runtime
allocates and frees their storage through device callbacks using DEVICE scope.
The CTS test installs an always-failing object allocator while retaining a
separate functioning device allocator. Creation succeeds without that object
allocator, then CTS validates both allocators' logs/lifetimes and emits a quality
warning because the object allocator was not called. No invalid callback or
incorrect OOM result was observed. Switching the ref-counted storage to the
public object's allocator would violate that allocator's lifetime; adding a
dummy allocation merely to change the test status would not improve behavior.

References inspected September20:
- Vulkan Memory Allocation, DEVICE-scoped allocations use the device/instance
  allocator: https://docs.vulkan.org/spec/latest/chapters/memory.html#memory-allocation
- src/vulkan/runtime/vk_descriptor_set_layout.c, vk_pipeline_layout.c
- CTS1.4.6.2 vktApiObjectManagementTests.cpp allocationCallbackFailTest,
  including validation of resCallbacks before the QualityWarning branch.

A separate pipeline-binary warning identifies an actual deduplication quality
issue: compilation-input hashes differ for some identical serialized binaries.
Public binary keys should identify serialized contents; the compilation cache
can retain its input hashes. Export/import tests are run separately for that fix.
