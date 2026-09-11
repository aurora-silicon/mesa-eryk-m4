# Saturated immediate FMA

Sauerbraten metl4 failed compilation at `ffma.sat #-8, r9, r11` after its map
loaded (141930, 95 thermal samples, peak51.5C, no GPU fault).

The accompanying Metal source was compiled offline with Apple AIR-NT32023.883,
macOS25G76, explicit applegpu_g16g target. Native text SHA256
b18cd849a89c4a80692fb4c880fff762d392f99153e6dbf2ac399a7fbf9a2bbc,
main offset64, FMA offset82: `09011ee1820202020082`. Exact composer decoding
identifies minifloat multiplier224(-8), saturation field14=1 and wait6.
The ALU core test reproduces this instruction byte-for-byte, retaining existing
negative-addend, wait/lifetime and unsaturated cases. This is compiler evidence;
the Apple sample itself was not executed.

`m4_saturated_fma.py OUTPUT` generates256 exact dyadic input pairs and2048
expected words for `m4_saturated_fma.comp`. Compile with glslangValidator -V;
run the existing guarded vk-buffer-oracle with eight groups. It checks four
clamped expressions (+/-8 multipliers and +/-0.5 with negated addend), two
unclamped controls and unchanged inputs. No CPU render path or fake output.

142646 hardware run without ASAHI_MESA_DEBUG=sync returned unchanged output
zeros (1022 mismatches). 142734 control with synchronous submission enabled
passed all2048 words, peak49C. The latter changed synchronization as well as
running warm: it does not isolate warmup or qualify asynchronous visibility.
The pre-existing completion/readback problem remains open. ARM64 Mesa build
and the new/existing host ALU fixtures pass; thirteen unrelated unsupported
forms remain explicitly skipped by that fixture.
