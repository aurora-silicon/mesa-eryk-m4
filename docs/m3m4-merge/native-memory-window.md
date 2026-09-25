# Specialize immutable buffer address-window geometry

Native buffer alias and strict residency lowering loaded the device's fixed
VA base and size from the root descriptor for every memory access. Keeping
those values dynamic prevented constant folding of 64-bit masks, page offsets
and range tests, adding arithmetic and register pressure to compute and
fragment shaders.

Replace only the two exact driver-root loads with the actual device geometry
before native optimization. Table pointers, alias entries and residency bits
remain dynamic. Cross-page splitting, robust access and all residency/alias
checks remain enabled. No shader resources or application features are removed.

The geometry is included in the pipeline/disk-cache identity. Device creation
rejects a logical-device window that differs from the physical device used to
build that identity. This avoids assuming identical kernel VA layouts across
M3, M4, or later kernel configurations.

Validation:
- Sparse/alias host tests run with both root-loaded and specialized geometry:
  208 byte/vector residency cases each, plus alias, atomic, descriptor, vector
  write-mask and cross-page cases.
- A separate high-address test verifies that only geometry becomes constant;
  both table pointers remain runtime values and a second pass is a no-op.
- AArch64 library builds. Native Vulkan gameplay completes on M3 and M4 and
  all three 3840x2160 screenshots match the retained reference exactly in RGB.
- M4 guarded evidence: 20260925T171259712662Z-satisfactory-native-memory-window.
  M3: 20260925T171433005881Z-m3-satisfactory-vulkan-gameplay-check.

The initial M4 diagnostic interval was 22.32 s versus approximately 24 s in
prior profiling runs. Separate uninstrumented repeated measurements are needed
before treating that difference as a performance result.
