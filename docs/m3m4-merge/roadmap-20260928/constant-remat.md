# Native constant rematerialization experiment

AGX_MESA_DEBUG=remat is an opt-in AGX3B compiler experiment. It uses the existing
constant-only rematerialization pass when its conservative register estimate can
save at least sixteen register halves. It avoids unspillable shaders, helpers, preambles and shaders
already needing scratch spills. It does not apply the older static occupancy
table to choose the target; the generic fallback retains the full register budget
if the estimate unexpectedly requires scratch. Normal compilation is unchanged.

The option occupies compiler debug bit13 and Vulkan compiler-cache bit57;
normal/experimental caches remain distinct. No NIR/key serialization changed.

All 123 unique compatible retained game shader inputs compile in both modes.
Sixteen binaries change, reducing reported register use by 18–48 halves but
sometimes adding substantial instructions. No hardware speedup is inferred from
this. The older shader statistics' threads field is not measured AGX3B occupancy.
The opt-in implementation reproduces all experimental binary hashes; with the
option absent, all 123 hashes match the original compiler. All 306 compiler unit
tests pass, and both host and ARM Vulkan builds pass. Hardware checks are recorded below.

Private input binaries remain in gpu-replay-bench. The adjacent profile records
input hashes, private provenance paths and before/after compiler statistics.

The final scratch-eligible implementation also passes exact byte-for-byte
comparison of all 123 ordinary (option absent) compiler outputs against the
original compiler. This is stronger than the initial 32-bit diagnostic-hash
comparison. The single ancillary host-timing round is not a compile-speed claim.

M4's initial opt-in binary passes 178 rendering/external-memory CTS cases (six
unsupported) and distinct/stable normal versus remat cache-UUID checks. The final
r2 binary produces exact Satisfactory native Vulkan reference images at frames
1/16/31 under the pre-existing six-option replay profile. This does not qualify
that profile or rematerialization as a general default. Completed timings are recorded below.

Final M4 measurements, five samples per mode with the last two pairs alternating:
normal median 3.4385 FPS (3.1815–3.5719), remat median 3.4548 (3.2720–3.5676).
The 0.47% median difference is smaller than the run variation: no demonstrated
Satisfactory speedup, so remat remains opt-in. Earlier 3.4100 FPS candidate runs
used remaining power-credit bursts; the matched r2 samples mostly use the
existing sustained-power policy. No thermal/power limits were changed.

M4 also passes all eight retained register-heavy compute/barrier oracles with
64–1024 threads and 32/80 live values, checking every output and unchanged input
byte. The final implementation's hash is 504a857ab73665be58a0f3d3a64800d2b92401e0a9a3009f9eae8f07b0917d85.
After its full CTS completed, M3 passed 178 rendering/external-memory cases
(six unsupported) and all eight register-heavy full-buffer oracles with the r2
binary. This establishes those correctness checks, not a performance gain.

The first M3 register-oracle attempt copied a legacy M4-only executable and
omitted the newer oracle's M4_ORACLE_DEVICE=M3 selection. It exited before
creating a logical device or submitting a shader. Its guard retained watchdog
recovery; original failure receipt and log are preserved. The current shared
oracle source already supports explicit M3/M4 selection; rebuilt it from the
recorded source and used that selector on the corrected attempt, which passed
all eight cases with eight verified kernel retirements. This is a
test setup failure, not a GPU correctness failure.
