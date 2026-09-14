# Remaining deterministic batch 1 cases: [30000,100000)

All 70,000 cases completed: 33,901 Pass, 36,043 NotSupported,
56 Fail, zero Crash/Timeout. Wall time including staging/guards: 2663.657s
(44m23.7s); sum of runner times: 2614.848s.
Peak recorded temperature: 61.75 C. All seven guards passed.
This is the initial outcome; none of these 56 failures has been retried or fixed.

The exact frozen catalog and global indices are preserved in selection.json,
per-case eligibility metadata and compressed results. Twenty deqp-runner workers,
50 cases per group, first-attempt execution, hard 60-second case timeout.
Every segment uses the existing 60-second watchdog and 0.5-second thermal guard.
No driver change or failure retry during this run. The earlier interrupted
[30000,40000) attempt is retained separately and is not counted here.

The CTS binary includes the previously documented reference corrections;
this is not an unmodified conformance run. Raw NotSupported does not establish
optionality. The skip audit is incomplete; no blanket optionality or required-support claim.
The user handed all 56 failures to Claude and paused our implementation work.
M4/lavapipe comparison is deferred until Claude is done. The full failure list
and per-case evidence are retained in failures.txt and failures.json.

## Why the CPU is mostly idle

A read-only process snapshot during segment [60000,70000) found 19/20 CTS
workers blocked in drm_syncobj_array_wait_timeout. The G16 backend uses a
one-credit DRM scheduler and permits one active hardware job. Twenty userspace
workers therefore do not imply twenty simultaneous GPU jobs.

A retained 16,000-line kernel-log sample covering 4,000 retirement records and
15.4695 seconds recorded 9.4553s last-command preparation, 4.8666s active interval
and 1.0793s cleanup. Active time includes publication, firmware and completion
observation; it is not pure shader execution time. These fields describe the
last command of each packet, not a general profiler for multicommand packets.
The raw sample is retained under build/cts-vulkan14-ordered/0030000-0100000/.

Compute::in_vm still creates a fresh 32 MiB private pool per dispatch; the recent
kernel optimization removed its unused firmware alias, but did not add reuse.
Render allocations already have a per-queue cache. Roughly 1,000 kernel log
lines/second also explain journald activity; this observation does not quantify
logging's causal share of elapsed time. Retirement telemetry currently supplies
the watchdog progress gate, so suppressing it requires preserving that signal.

Future optimization targets: safely reuse compute resources after retirement,
reduce routine diagnostic formatting while retaining independent progress,
and implement properly owned concurrent in-flight jobs. No such changes were
made in the middle of this run.

## All 56 failures resolved: CTS reference corrections, no driver change

Both families were defects in the earlier separately documented CTS
reference correction, not Mesa or kernel bugs. The live driver library
(SHA256 53b31f227547728c5655e21cf879982e4fdda2adce1950da9e6e22b3bcf74322)
and Asahi module were not changed.

A diagnostic CTS build (`blit-diagnostic-build-manifest.json`, SHA256
3a2375af6b3f93a11b7bb0a6fe0c37ea54d93567c767f9bb8b7178fbdb1b9fdb) logged
the numeric mismatches for three representative cases without changing
verdicts (`blit-diagnostic1-values.txt`, evidence
20260914T234915806567Z-claude-cts-diag1, peak 63.75 C):

- Nearest blits into B10G11R11_UFLOAT: the GPU stores exactly the
  round-toward-zero conversion of the source texel (255 -> 254 in 11 bits,
  255 -> 252 in 10 bits; 415 -> 412 versus the round-to-nearest 416). The
  AGX3 tile store packs 11/11/10-bit floats in software with
  `nir_format_pack_11f11f10f`, which truncates the half-float mantissa.
  The Vulkan "Floating-Point Format Conversions" rules state that finite
  values between two representable values are rounded to one or the other
  and that the rounding mode is not defined; negative values become zero
  in unsigned formats. The earlier correction compared with zero tolerance
  against tcu's round-to-even conversion, which is one permitted result.
- Linear blits from 8-bit USCALED into A2B10G10R10/A2R10G10B10_USCALED:
  the reference expected alpha values such as 247 in the two-bit alpha
  channel. tcu's format info applies the single 0..1023 range to every
  channel of the packed scaled type, so the earlier clamp never limited
  alpha to 3. vkCmdBlitImage first clamps and then casts to the destination
  format; the GPU wrote 3. Three-channel B8G8R8_USCALED sources passed only
  because their alpha reads as 1.

`cts-scaled-blit.patch` supersedes `cts-scaled-compute.patch` as the
cumulative CTS correction. It derives the scaled destination clamp per
channel from the bit depth, and compares nearest scaled-to-packed-float
results per channel against both adjacent packed-float conversions
(round downward and round upward, tcu `Float11`/`Float10`) with zero
tolerance, still through the lookup verifier's coordinate precision. It
does not change source image generation, thresholds for other formats,
or any driver behaviour. Corrected binary: `/root/cts-1.4.6.2/deqp-vk-seed30-scaled-blit`,
SHA256 36bcc144d4b3c0edc5b638f20331fbc4e1be388d89da652890af870b8e5697dc
(`cts-scaled-blit-build-manifest.json`; sources and the original executable
were restored after the build).

Results with the corrected binary and unchanged driver:

- All 56 original failures pass: `blit-fix1-summary.json`,
  `blit-fix1-results.json`; 5.629 s runner time, eight workers, 60-second
  case bound, peak 59.5 C. Evidence 20260914T235558154495Z-claude-cts-blit-fix1.
- Eight previously passing neighbours through the changed comparison paths
  (unorm/snorm/compressed/RGB9E5 sources into scaled or packed float
  destinations, scaled-to-scaled nearest and linear) still pass:
  `blit-witness1.txt`, `blit-witness1-summary.json`, peak 59.0 C.
  Evidence 20260914T235638787757Z-claude-cts-blit-witness1.

The original 56 Fail outcomes above remain the recorded first-attempt
results. These cases are in the Khronos vk-default mustpass; the adapted
comparison is documented here and is not an unmodified conformance result.
No broad regression rerun was performed, as requested. The r-first scaled
sources (for example R8G8B8A8_USCALED) sit in the unrun [100000,200000)
range and were not exercised.
