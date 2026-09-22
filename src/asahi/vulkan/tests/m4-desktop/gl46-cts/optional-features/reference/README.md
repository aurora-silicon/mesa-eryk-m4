# CTS sparse-depth setup correction (diagnostic)

The unmodified v1.4.6.2 clamp-color tests fail six optional depth-format cases
before rendering. The parent sparse lookup loop checks NUM_VIRTUAL_PAGE_SIZES,
but this subclass does not. It also sizes non-sparse textures from sparse page
sizes (zero for unsupported sparse formats), and writes DEPTH_COMPONENT16 using
an R16 storage-image binding, which is not a legal image format for that texture.

The patch retains ordinary depth-clamp coverage: skip only unsupported sparse
function/format combinations; choose nonzero dimensions for ordinary textures;
fill depth mip levels with glClearTexImage. It does not change driver limits,
expected sampled values or shader comparisons. Upstream result files remain
unchanged. Patched-binary results must be reported separately.

Spec: https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sparse_texture.txt
Only the listed color formats require sparse allocation; D16 is optional.

All six affected cases pass on M4 with the diagnostic binary, including the
ordinary shadow LOD-clamp comparisons. Evidence:
`20260922T050841594484Z-gl46-clamp-depth-diagnostic`.
The driver is unchanged from the unmodified six-failure run. This validates
the test correction; it is not six passes on the original CTS binary.
