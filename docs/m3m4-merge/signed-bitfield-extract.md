# Fixed signed bitfield extraction

For a valid 32-bit field of width W at offset O, sign extension is
`(int32_t)(value << (32-W-O)) >> (32-W)`. The left shift already discards
bits above and below the selected field; a preceding unsigned extraction is
redundant. Omit zero shifts explicitly because this is a late lowering and
native ASR immediate zero has no qualified encoding. Dynamic and zero widths
retain the existing lowering.

The retained large Satisfactory compute input starting `f5536fd4` shrank from
22,838 instructions / 249,930 bytes to 20,247 instructions / 218,830 bytes in
the initial candidate. This is an integer identity, with no floating-point
reassociation or application-specific selection.

Validation of corrected AArch64 library
`96927601bd630f1adcb16f4caa3d661baf19c7a8e98a096efe98a3cf1eb2bfa3`:

- All 296 host compiler tests pass.
- `validation/signed-extract-oracle.py` generates every one of the 528 legal
  constant offset/width pairs against 512 runtime words, plus variable pairs
  and zero-width cases. Both M3 Pro and M4 match all 272,896 expected words.
  Hardware execution uses vk-buffer-oracle under the mandatory target guards.
- Frozen Satisfactory Vulkan gameplay replay, output 3840x2160, captured
  internal 2880x1620: frames 1, 16 and 31 match the existing reference RGB
  exactly on both M3 Pro and M4.
- Three M3 timing repeats: 9.537297148, 9.481197321, 9.587001240 seconds for
  30 completed frames. Median 9.537297148 seconds, 3.14554 replay FPS.
- Three M4 repeats: 13.808839765, 13.372431315, 14.363268583 seconds.
  Median 13.808839765 seconds, 2.17252 replay FPS. Thermal variation remains
  part of these guarded device measurements.

These are saved-replay results with the previously recorded replay adaptations,
not live gameplay measurements or a general conformance claim. Application
compiler captures, rendered images, guard telemetry and failed development
experiments remain only in the private local gpu-replay-bench repository.
