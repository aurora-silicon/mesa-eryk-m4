# Repeated immutable samples on native M3 fragments

`HK_NATIVE_REPEATED_SAMPLER=true` is an opt-in G15S Vulkan experiment. The
default and other chips are unchanged. Its policy occupies compiler flag bit
61, including shader and pipeline cache UUIDs.

The existing single-sample texture0/sampler0 ABI can also serve multiple
statically addressed samples of the same set-zero combined descriptor. The
new lowering removes their register handles while retaining unrelated UBO
descriptor reads and the separate fragment root. It runs only in the direct
UBO variant after ordinary-sampler specialization. The existing per-draw
proof checks reductions, image-view minima, scaled formats and border handling
before selecting that executable. Update-after-bind and descriptor-buffer
layouts, dynamic handles, different resources, unsupported operations and
unproven biases retain the general executable. Spilled tilebuffers also keep
the general variant: their global texture heap cannot share a rebased prefix.

Allocated and push descriptors use the same resolved GPU set address. Native
entry-zero texture records are 32-byte aligned; unaligned inline software
descriptors are copied into immutable command-owned storage. Identical bytes
reuse the last copy, and command reset clears that cache. This does not cache
UBO contents or assume that different push allocations have identical data.

An initial prototype dereferenced a software descriptor at offset 144, hit an
invalid GPU address and returned device loss. The mandatory guard saved a
sealed firmware snapshot and kernel records, withheld watchdog renewal, and
recovered by reboot. The aligned implementation completes the original replay
and matches all three retained RGB reference images exactly. The failed build
and evidence remain identified separately; they are not passing measurements.

The selected Minecraft world fragment changes from 267 to 259 compiler IR
instructions and from 88 to 72 linked register halves, with no scratch. This
is not an instruction-cycle estimate. The shader still performs all samples.
Ten lowering cases cover repeated samples plus a live unrelated UBO, different
resources, another set, dynamic offsets, unsupported fetches, shader-stage and
sample-count exclusions, explicit bias, an unaligned inline offset and integer
overflow. The focused root tests and all 326 compiler tests pass. The full
graphics fixture still has its previously recorded byte-expectation failures;
the unchanged baseline produced the same output.

Two same-boot nine-pair studies render all 30 completed 3024x1837 gameplay
replay frames, including final completion and excluding setup, warmup and PNG
work. The first aligned implementation gives 177.00/178.55 FPS and a 0.897%
median paired gain (8/9 positive). The immutable-copy memo rebuild gives
176.43/178.35 FPS and a 1.217% median paired gain (9/9 positive). These studies
compare flag false/true within each binary; they do not isolate the memo's
own effect across binaries.

The memo rebuild's pinned 1,268-case texture selection gives 927 Pass,
341 NotSupported, zero Fail, timeouts or device losses. Catalog hash and
global indices are preserved. Every successful hardware segment has a verified
60-second watchdog, all-channel 0.5-second thermal telemetry and restored
kernel controls. This is focused qualification, not full conformance, native
OpenGL or M4 qualification. These are replay FPS, not live Java FPS, and they
remain below the refreshed approximately 265 FPS macOS reference. The normal
desktop prefix and benchmark HTML are unchanged.

Evidence: gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/
repeated-sampler. Final Vulkan DSO SHA-256:
`032744ac68445fd8d92accda6619045142d13bef98e26f0733bde8ba2b04b21a`.
