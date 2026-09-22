# M3 GLES shader compilation crash fixes (2026-09-22)

The ES3.1 validation stack is Zink -> Honeykrisp -> the native M3 kernel
runtime. No native Gallium Asahi GLES claim or ES3.2 enablement is made.

The compiler fmov optimizer folded f16-to-f32 widening into DFDX and FFMA
operations. The AGX3b encoder requires full-width inputs for these forms.
Examples were `r40 = dfdx r29h` and
`r7 = ffma r1h.neg, r3, #0.500000`. Encoder rejection returned
VK_ERROR_FEATURE_NOT_PRESENT; the observed Zink stack subsequently bound
a null pipeline and crashed. Preserving the explicit widening lets the
original shaders compile and execute without changing CTS references.

Regression coverage includes DFDX, DFDY and modified FFMA. All 34 optimizer
tests pass. The new regression fails with the original optimizer.

Running the full compiler test executable exposed assertions in the error
printer for invalid wide-register alignment and minifloat immediates. The
negative encoder tests deliberately supply those operands. Diagnostics now
print explicit invalid-operand labels and preserve the encoder rejection.
The entire 290-test binary completes: 240 Pass, 50 existing failures. With
the original optimizer and the same printer repair, those same 50 tests
fail, plus the new widening regression. This is not a fully passing compiler
unit suite; the unchanged failures are not counted as passes.

## Hardware evidence

Kernel with the M3 tiler arena fix, boot
`17b078ea-6750-48d2-88db-9225f3f8abc4`. Mesa compiler width fix in an isolated
prefix; the pinned CTS binary is unchanged. One worker, private headless
Weston/Xwayland, no GL version override. Each test segment uses the M3
45-second guard, 60-second watchdog, and 0.5-second five-channel temperature
sampling with the existing 95 C stop.

- `build/m3-gles31-20260922/triage-compiler/width-fix`: 3 Pass, guard complete, peak 48.75 C; result SHA256 `8b2e483c22a278d1e551867e6049fcd9bce9c7b385bfd439d506fa9a16134d35`.
- `build/m3-gles31-20260922/triage-compiler-neighbors/00/run`: 40 Pass, guard complete, peak 49.50 C; result SHA256 `5c48dae83b4331db940d7cb391fde79cb94186b25d0773c44b318c36e86a9d96`.
- `build/m3-gles31-20260922/triage-compiler-neighbors/01/run`: 40 Pass, guard complete, peak 48.00 C; result SHA256 `7b34ef417d991cdc902e77a41e9222838d64485d6255b54b629a6f1884ddbc54`.
- `build/m3-gles31-20260922/triage-compiler-neighbors/02/run`: 40 Pass, guard complete, peak 47.50 C; result SHA256 `ce580ffd8fa0c750dc3342c947552e7463a52bee4ead13bbcbf966eb91534cbf`.
- `build/m3-gles31-20260922/triage-compiler-neighbors/03/run`: 5 Pass, guard complete, peak 47.50 C; result SHA256 `f34e1ca9c398d39cb321bbb0167bca429a9be0094c05e76ac2e07dcbc4056cc9`.

128 passing executions: the three exact crash reproducers, all 75 discard
cases and all 50 random trigonometric fragment cases. No compiler rejection,
process crash, GPU fault or reboot occurred in these focused runs. These
results do not replace original campaign outcomes or establish full GLES
conformance. The broader suite remains paused during crash repair.
