# GLSL acos precision on M3 (2026-09-22)

Four GLES3 mediump fragment acos precision cases (scalar through vec4)
returned QualityWarning. Example: input `0x1.ff8p-1` produced `0x1.68p-5`,
outside the expected interval `[0x1.69p-5, 0x1.6b8p-5]`.

The GLSL builtin computed pi/2 - asin(x), and asin itself subtracts a small
polynomial/square-root product from pi/2. Rounding these intermediate values
to mediump loses precision through cancellation near x=1.

Evaluate the same polynomial/square-root product directly for positive x,
and pi minus that product for negative x. Coefficients and test references
are unchanged. This is a GLSL compiler correction; it does not change the
SPIR-V extended-instruction asin/acos lowering or add an advertised feature.

## M3 validation

All 44 selected acos/asin precision cases pass, including the original four
warnings. Another 443 GLES2/GLES3 acos and randomized trigonometric cases
pass. Exact session options and frozen global indices are retained. Original
QualityWarning records remain unchanged, with passing repair evidence linked
in the campaign triage.

Zink -> Honeykrisp on M3 Pro, same kernel/boot as the preceding fixes. Each
segment used the 45-second guard, 60-second watchdog and five-channel 0.5 s
thermal sampling with the 95 C stop. No crash, GPU fault or reboot occurred.
This is focused regression coverage, not a full conformance claim.

- `build/m3-gles31-20260922/triage-acos/stable-form`: 44 Pass, peak 54.00 C; results SHA256 `bf4f91b4251167d58a4a5cb75ad55e002811a050d1dbdfec414bfa393ca57570`.
- `build/m3-gles31-20260922/triage-acos-neighbors/1-00/run`: 80 Pass, peak 50.00 C; results SHA256 `671902c2b39c75ca1e4bbe4a0486f1633e3bc3ad89743a0b089e042c4d1864f6`.
- `build/m3-gles31-20260922/triage-acos-neighbors/1-01/run`: 80 Pass, peak 49.00 C; results SHA256 `4f00281d9195e3152f14caccf04b61b554976d54b4f9b2a8a6fd92265a79a7da`.
- `build/m3-gles31-20260922/triage-acos-neighbors/1-02/run`: 56 Pass, peak 48.00 C; results SHA256 `5f9eee7f73e58c4dd67a7d11d6b383e7064617e13dddc3c8bb133fcae8ae422b`.
- `build/m3-gles31-20260922/triage-acos-neighbors/2-00/run`: 80 Pass, peak 50.75 C; results SHA256 `95f92e00fe4ac264e60ee4267f5cb88e2528155404132b6646aa539d2d7d10ef`.
- `build/m3-gles31-20260922/triage-acos-neighbors/2-01/run`: 80 Pass, peak 50.50 C; results SHA256 `272e567698fb188c8f7c05ff5ae156a5120c8e86c29194cd90296add0e129bc4`.
- `build/m3-gles31-20260922/triage-acos-neighbors/2-02/run`: 67 Pass, peak 49.75 C; results SHA256 `4bb71128fec08e002888c33d275e2093046dd888a104adc237ea056a5a97dfea`.
