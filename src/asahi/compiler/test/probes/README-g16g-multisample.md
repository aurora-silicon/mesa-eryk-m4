# G16G multisample investigation

Native four-sample fragment pipelines compiled offline for applegpu_g16g
show that LLDCOL/LSTCOL f51 selects the operand kind, rather than a
single-sample/per-sample mode: f51=1 carries an immediate mask in f43;
f51=2 carries a register in f44. The immediate masks 1, 2, 4, 8 load
individual samples. Exact native load bytes are regression vectors in
`test-agx3-tile-core.c`. The legacy codec API retains its mask of one.

Use `native-metal-compile.py SOURCE --stage render --function fs --samples 4
--output NEW_DIRECTORY` for a native render pipeline. Sample count defaults
to one. This is offline compilation, not a GPU support claim.

Workspace evidence: `_provenance/chromium-reference-20260912/msaa-native/`
contains MSL inputs, compiler versions, invocation records and binaries for
`sample-fragment4`, `solid-fragment4`, and multisample texture reads.
The fragment fetch object is `sample-fragment4/text-10a0.bin`, main entry64;
its four load instructions start at76,88,100,112.

At this checkpoint the bounded Vulkan/GL hardware probes still fail during
MSAA attachment store/reload. Neither four-sample support nor Chromium ES3
is qualified. Do not infer hardware validation from byte-exact encoders.
