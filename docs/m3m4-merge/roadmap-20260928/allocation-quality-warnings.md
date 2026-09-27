# Four allocator QualityWarnings, preserved separately from failures

Both frozen campaigns report the same four cases: descriptor_set_layout_empty,
descriptor_set_layout_single, pipeline_layout_empty, pipeline_layout_single in
`dEQP-VK.api.object_management.alloc_callback_fail`. Their global indices are
327169, 327170, 327195 and 327196. Exact original records and segment identities
are in `allocation-quality-warnings.json`; these have not been changed to Pass.

The pinned CTS `vktApiObjectManagementTests.cpp` allocation-failure loop creates
an object while allowing zero successful object-allocator calls. When creation
succeeds without such calls it reports QualityWarning, except for its explicitly
pooled object types. It did not observe an incorrect OOM result or invalid
callback invocation in these four cases; it could not inject an allocation
failure into the object's allocator.

The shared Mesa path explains the observation. `hk_CreateDescriptorSetLayout`
uses `vk_descriptor_set_layout_multizalloc`. That helper allocates through
`device->alloc` with DEVICE scope. The common pipeline-layout constructor does
the same. Both common destroy entrypoints ignore the object allocator and unref
the layout; actual freeing uses the corresponding device allocator. Source
comments explicitly connect this choice to reference-counted lifetimes that can
outlive the API object handle. These are common Vulkan runtime paths, not M3 or
M4 shader/code-generation differences.

Do not allocate/free a dummy object just to make the test's callback counter
nonzero. Such a change would not test the allocation that actually holds layout
state. This classification explains current warning behavior; it is not a
blanket proof of every allocation/lifetime contract. P2 must still test real
DEVICE-scope allocation failures and partial layout construction/unwind.
