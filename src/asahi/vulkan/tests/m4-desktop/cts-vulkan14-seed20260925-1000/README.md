# Vulkan 1.4 CTS: 1,000 random Linux cases, seed 20260925

Official [vulkan-cts-1.4.6.2](https://github.com/KhronosGroup/VK-GL-CTS/releases/tag/vulkan-cts-1.4.6.2),
commit `f6a29701220f34dd1407513bfe80d74ca7b392ce`, built separately for ARM64.
The CTS source and expectations are unchanged. The only build correction was
selecting the host wayland-scanner instead of the ARM64 sysroot executable.
No Mesa/kernel rendering fixes were made during this sample.

The user explicitly selected a broad sample with skips reported separately.
Recursive expansion produced 3,247,552 unique mustpass cases; the existing
Linux name filter excluded 41,522 Windows/Android entries. Python
`random.Random(20260925).sample` selected 1,000 from the remaining 3,206,030.
This is an unclassified requirement pool, not the old reviewed 1.0 pool.
Some Android AHB extension cases do not match that name filter; they remain
visible as optional extension skips. No cases were replaced based on results.

| Audited result | Count | Rate |
| --- | ---: | ---: |
| Pass | 373 | 37.3% |
| Fail | 91 | 9.1% |
| Crash | 2 | 0.2% |
| Timeout | 0 | 0.0% |
| NotSupported | 526 | 52.6% |
| NotSupportedButShouldBeSupported | 8 | 0.8% |

Raw CTS reports 534 NotSupported results. The extra category is an offline
review of the **reported skip reason**, preserving the original QPA status:
five reject `sampleRateShading` (required by Vulkan 1.4), and three reject
`multiview` (required since Vulkan 1.1). Both are false in the captured profile.
Some of these cases also request optional configurations, including sample
counts or formats; fixing the mandatory feature can expose a different valid
skip. The category does not assert that every later prerequisite is mandatory.
The profile also reports `robustImageAccess=false`, contrary to Vulkan 1.3+
requirements, but none of these sampled skips names that gate.

All 123 distinct skip reasons were reviewed against their exact frozen cases,
the captured capabilities, pinned CTS support gates, Vulkan registry and format
requirements. `skip-review.json.gz` contains the case-bound rationale for each
review. Optional queues, extensions, format/tiling combinations, sample counts,
limits and presentation modes remain NotSupported. In particular, defining a
format such as A8_UNORM does not require all its image-format feature bits.
No skip was relabeled as a pass, and no GPU test was rerun for this audit.

## Timing and recovery

20 workers, groups of 50, explicit 60-second per-case timeout, no automatic
retries. Three execution segments took 11.902 + 13.510 + 5.652 = **31.064 s**.
Guarded execution plus evidence transfer took **40.064 s**, staging **4.431 s**.
Wall span from first actual test start through final result was **230.140 s**,
including inspection, Xwayland access setup and resumption pauses. Download,
build and the pre-test display-recovery reboot are excluded from those timings.

Two process crashes stopped their segments; 61 worker attempts were interrupted
and explicitly resumed after checking device health. Completed Pass/Fail/Crash
results were never retried or overwritten. All 1,000 cases have terminal
outcomes; `results.jsonl.gz` preserves every attempt and original reason.
These timings describe an interrupted sample, not uninterrupted throughput.

Before any case ran, a preflight caught a preexisting DCP failure. Its kernel
log explicitly required reboot. Evidence was retained and a guarded reboot
restored DCP to zero failures. This setup attempt completed zero tests and is
recorded in `setup-failure.json`, separately from the actual campaign.
All three test segments then used boot `4677c960-6288-422d-b849-67cddc408032`.
No GPU reset or reboot occurred during their execution. All guards used the
60-second watchdog and six-channel thermal monitoring; peak was **63.75 C**.
The post-reboot CPU ceilings were 1.80/1.86 GHz as recorded in thermal evidence.

The runner discovered SDDM's compositor before user login. Xwayland initially
rejected the root test client; before resuming unfinished WSI cases, only
`SI:localuser:root` was temporarily authorized. That permission was revoked
after execution. Final status retains SDDM active, DCP failed=0 and the watchdog
armed in idle mode. No default-boot configuration was changed.

## Reproduction evidence

- `source.json`, `build-command.txt`, `arm64-toolchain.cmake`, `cts-binary.json`:
  exact CTS/dependency revisions, build setup and executable hash.
- `mustpass.json`, `selection.json`, `cases.txt.gz`, `cases.meta.json.gz`:
  all included list hashes and the frozen sample.
- `device-profile.json.gz`: exact driver-bound Vulkan capabilities.
- `summary.json`, `results.jsonl.gz`, `failures.tsv`: original CTS outcomes,
  attempts, timing and segment software identities.
- `audited-summary.json`, `audited-results.json.gz`, `skip-review.json.gz`:
  reviewed presentation of the original skips, with case-specific rationale.
- `segment*-evidence.tar.gz`: raw grouped QPA/stdout, compiler refusal captures,
  target identity, runner invocation/environment and watchdog/thermal evidence.

The first crash is a Mesa assertion in `hk_descriptor_stride_align_for_type`
(`hk_descriptor_set_layout.c:101`, stride exceeds HK_MAX_DESCRIPTOR_SIZE) for
`binding_model.mutable_descriptor.single.switches.combined_image_sampler_storage_buffer.update_write.no_source.no_source.pool_no_types.pre_update.no_array.vert`.
The second is SIGSEGV in
`texture.swizzle.component_mapping.color.a2b10g10r10_uscaled_pack32_2d_pot_gggg`.
All case names carry the `dEQP-VK.` prefix; use the retained failure list directly.

The 91 failures include 69 FEATURE_NOT_PRESENT returns at command-buffer end
or pipeline creation, two FORMAT_NOT_SUPPORTED returns, two null shader-object
checks, and 18 other failures including output mismatches. Failures and crashes
are retained for subsequent fixes. This random sample is not conformance.
