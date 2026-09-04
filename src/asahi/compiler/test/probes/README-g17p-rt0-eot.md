# G17P RT0 end-of-tile compiler witness

This fixture was regenerated on 2026-09-04 with Apple Metal Toolchain
32023.917 (`metal-as` reports Apple LLVM 32023.917, `metallib` reports
AIR-LLD 32023.917, and `metal-nt` reports AIR-NT 32023.917). The target is
`applegpu_g17p`. The source SHA-256 is
`ef6ba0d00cd62dfc1ebec054defdd2aa087bacd4501593a6ff6a86570385dd41`.

Apple's native translator only emits a GPU Mach-O through pipeline-image mode.
The recipe uses the clean-room `compute.mtlp` pipeline image at
`research/gpu/isa-re/agx3/offline/compute.mtlp`, SHA-256
`0b5942e1b16effa6d73c8a91dd3c263d27d43daa9632a8b71d8ddabcf7cd07f9`.
It names `rt_tri_oneshot` and expects the metallib under the placeholder below.

```sh
TC=/path/to/Metal.xctoolchain/usr/bin
PIPELINE=/path/to/research/gpu/isa-re/agx3/offline/compute.mtlp
NAME=_Path_not_available_for_lib_from_source_with_UUID_90AED5BA74F907EF4A6113CEF205564B72A14DF0A886AA9F83C37995ED529342
WORK=/private/tmp/g17p-rt0-eot

mkdir -p "$WORK/lib"
"$TC/metal-as" g17p-rt0-eot.ll -o "$WORK/g17p-rt0-eot.air"
"$TC/metallib" "$WORK/g17p-rt0-eot.air" -o "$WORK/g17p-rt0-eot.metallib"
cp "$WORK/g17p-rt0-eot.metallib" "$WORK/lib/$NAME"
"$TC/metal-nt" -N "$PIPELINE" -L "$WORK/lib" -arch applegpu_g17p \
  -platform_version macos 26.0 26.0 -o "$WORK/g17p-rt0-eot.nat"
```

Extract `__TEXT,__text` from the nested applegpu Mach-O. It is 128 bytes and
must match `g17p-rt0-eot.expected`, SHA-256
`4dc6a4f9e4e415012a0c0d4a415591b49b360e24e2eb66fd4b4b38308fd1b691`.
The main starts at `+0x40`; its SHA-256 is
`22e75467d14635b3c602ab7b5ce1c6a315ec259cb5dff0842867c0c535795c8f`.
It reads thread-position X/Y, emits `pack_imgwir`
`970554040000000050501000`, and ends with the RT0 `imgwblk`
`5740540000000080aa750200`. These operands correspond to Mesa's bounded
non-arrayed, single-sample, 2D RT0 block store at tilebuffer byte offset zero,
with physical format `PIPE_FORMAT_R8_UNORM`; every other axis stays
fail-closed.
