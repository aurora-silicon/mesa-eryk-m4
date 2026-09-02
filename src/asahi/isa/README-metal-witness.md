# Getting Apple's own G17P encoding for any shader shape

Apple's Metal toolchain is installed on the development Mac and targets our exact
part directly.  This turns ISA questions from inference into witness: write MSL,
get Apple's byte-exact `applegpu_g17p` machine code, diff against ours.

## Toolchain

    MT=$(dirname $(xcrun -f metal))     # Metal.xctoolchain/usr/bin
    "$MT/metal-nt" -archs               # applegpu_g17p is in the list

`applegpu-nt` and `metal-nt` are the same AIR Native Translator driver.

## Harness

`/tmp/g17p-re/g17probe/gen.py` does MSL -> AIR -> metallib -> native pipeline and
extracts `__text`.  Copy it somewhere durable before relying on it.

    import sys; sys.path.insert(0, ".../g17probe")
    from gen import build
    build(name, msl, pipeline, outdir, arch="applegpu_g17p", kind="compute")

### The pipeline-script shape (this is the part that costs an hour to rediscover)

`metal-nt` requires `-N <script>`; without it you get
`error: cannot translate functions in '<lib>'  note: specify a pipeline script`.
The script is `.mtlp-json`.  For a COMPUTE pipeline the entry is:

    {"compute_function": "<kernel name>"}      # a bare STRING, not an object

`{"compute_function": {"library": "lib0", "name": ...}}` and adding `"library"`
or `"label"` siblings are all parse errors.  Render pipelines use the
`render_pipelines` key instead of `compute_pipelines` (see `gen.py`).

## Reading the output

Code starts after the container's leading blocks; find `__text` (gen.py does).
The first 0x40 bytes before the entry are the constant program (`0e 00 00 00`
then `06 00` filler) -- the same code-container layout the driver builds.

## Standing hazard: field positions differ per instruction FORM

The XML field *names* and bit positions are per-instruction.  Confirmed traps:

    base kind :  lddev = bit 101      stdev = bit 99
    index kind:  stdev = bit 100
    access kind: lddev = f75 @97:99   stdev = f73 @95:97
    16-bit off : lddev = f76 @77:92   stdev = f74 @75:90

`lddev.f75` and `stdev.f75` are DIFFERENT PHYSICAL FIELDS.  A decode rule pinned
on one form does not transfer to the other.  Map each form separately, by
varying one field at a time from a known-good baseline through
`agx3_mem_encode_load/store` or `agx3_compose`.

Mapped, both forms: wait mask = bits 12..17 (wait slots 1..6);
lddev signal slot = bits 70..72; lddev uniform base = byte4 = base/4.

## Corpus

- `src/asahi/compiler/test/agx3_compose_corpus_vectors.c` -- 1,494 real Apple
  vectors with field values, incl. 78 `lddev` / 16 `stdev`.  Use one as a
  baseline: an all-zero field vector does not satisfy the form's predicates.
- m1n1 `proxyclient/experiments/g17p_grid_helper_code.py` -- a captured 16 KiB
  Apple code image, 8 shaders, all of which disassemble cleanly to `end`.
