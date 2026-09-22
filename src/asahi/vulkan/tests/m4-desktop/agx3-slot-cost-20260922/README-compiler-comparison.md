# Offline compiler comparisons

`compiler-binary-compare.py` checks exact output bytes from two host
`agx_shader_replay` executables, then measures paired corpus compilation wall
times with alternating execution order. It requires the replayer's
`--binary-output=FILE` option. Inputs are a JSON list of retained `input.bin`
paths; the output directory must not exist. Each process is bounded to 30 s.

```sh
python3 tools/compiler-binary-compare.py \
  --before build/BEFORE/agx_shader_replay \
  --after build/AFTER/agx_shader_replay \
  --inputs build/inputs.json --output build/NEW_COMPARISON --rounds 20
```

Run timing comparisons without concurrent builds or other host CPU loads.
Output includes each input/executable/binary SHA256, exact binary files,
compiler logs, round totals, medians and the median paired reduction. Timings
include process startup. They do not measure GPU execution or game FPS.
Capture/debug environment is disabled during this comparison; other AGX,
Asahi and Mesa environment settings are recorded. Reuse retained inputs.

`agx3-slot-differential.c` compares the live slot allocator with a retained
original implementation over 2,048 deterministic streams / 262,144
instructions, spanning 1–8 slots, token-domain boundaries, sparse stack
tokens, drains, read/write dependencies and required slots:

```sh
cc -O2 -Wall -Wextra -Werror -Imesa/m4-asahi/src/asahi/compiler \
  -Dagx3_slot_assign=agx3_slot_assign_before \
  -c build/slot-before.c -o build/slot-before.o
cc -O2 -Wall -Wextra -Werror -Imesa/m4-asahi/src/asahi/compiler \
  build/slot-before.o mesa/m4-asahi/src/asahi/compiler/agx3_slot_core.c \
  tools/agx3-slot-differential.c -o build/slot-differential
timeout 60 build/slot-differential
```

These tools access no GPU or target host. They supplement the allocator's
unit tests and guarded hardware validation; they do not establish full
driver conformance. September 22 evidence is mirrored in Mesa under
`src/asahi/vulkan/tests/m4-desktop/agx3-slot-cost-20260922/`.
