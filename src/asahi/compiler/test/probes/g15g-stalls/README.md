# G15G scheduling-stall witnesses

Sources and scripts behind `../../../README-g15g-scheduling-stalls.md`. No
Apple binaries are kept here; `run-corpus.sh` regenerates everything with the
Xcode Metal toolchain (offline compilation only, no GPU) into a work
directory and checks both the Python reference model and Mesa's C pass
against Apple's G15G placement:

    sh run-corpus.sh [/tmp/g15g-stalls-work]
    AURORA_MESA=../mesa sh run-corpus.sh    # + Aurora's integer/optimization corpora

| file | role |
| --- | --- |
| `gen_corpus.py` | writes the 274-kernel main corpus (`kernels.metal`, `cases.txt`) |
| `targeted.metal`, `render.metal` | hand-written witnesses (compute; vertex+fragment pipelines) |
| `fuzz.py`, `fuzz2.py` | random kernel generators (seeds in `run-corpus.sh`; `fuzz2.py` output was the holdout) |
| `compile.py` | MSL -> AIR -> metallib -> `metal-nt` for g15g/g15s/g16g, extracts `__text` (`macho.py`) into JSON |
| `agx3dis.py` | instruction walker over Mesa's `AGX3b.xml` |
| `decomp_batch.c`, `fields.py` | field-vector decomposition with the composer tables (cached in the work dir) |
| `fieldbits.c`, `autobase.py` | which encoded bits a field drives; derives wait-field ids into `wait-base.json` |
| `stalls.py` | finds stall groups, compares stripped G15G with G15S |
| `predict.py` | reference model of the rule; env switches for ablations |
| `evidence-table.py`, `show.py` | per-consumer/producer stall tables; annotated listing of one shader |
| `claim.py` | "G15G minus stalls == G15S up to branch displacements" statistics |
| `mkvectors.py`, `gen-test-vectors.py` | test vectors for `test-agx3-g15g-stalls.c` and its built-in header |
