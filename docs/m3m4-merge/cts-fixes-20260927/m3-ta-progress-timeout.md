# J514S TA progress timeout, 2026-09-27

The original frozen Vulkan catalog case 2713005,
`dEQP-VK.spirv_assembly.instruction.graphics.float16.logical.opfordequal_vector_tesse`,
faulted in isolation on both the current and previous Mesa. G16G passed it.
The SIGKILL reported as CTS Crash followed the guard observing unhealthy GPU
state; it was not the primary failure. No sampled MMU fault bits were set.

## Evidence and cause

The shader executes 38,416 loop iterations in the native vertex/tiling path.
An explicitly incomplete 16,384-iteration diagnostic retired in 15.25 ms and
failed its expected-output comparison, as expected. It is not a CTS pass.
The full test using the diagnostic direct-buffer shortcut retired correctly in
18.80 ms, but that shortcut remains disabled because of independent retained
game-image regressions. Disabling register-cache hints, serializing async vertex
operations, and changing code motion did not fix the normal path.

RTKit 2419's `progress_check_interval_ta` is Globals+0x980. Its firmware copies
this into a cached threshold at text+0x6514; the TA path selects it at +0xd70c,
compares the per-engine poll count at +0xd710..0xd72c, and increments that count
at +0xe42c..0xe434. This is a firmware poll threshold, not the host fence timeout.
The retained firmware text SHA-256 is
`2e25dacda9635434242ac8105d230aca881aa18830183e467f9393237b75ca23`.

Changing only that threshold from the inherited 10 to 100 allows the unmodified
case, normal Mesa addressing and full loop to complete correctly in **35.73 ms**
of tiling time. The old threshold rejected valid long vertex work before its
primitive retired. This establishes a premature progress timeout for this case;
it does not establish that every future event 4 has the same cause.

The finite 100-poll policy preserves firmware progress detection, the host job
bound, health latch, retained pending mappings, thermal protection and the
60-second system watchdog. It trades slower detection of a genuinely stuck TA
job for enough time to execute valid shaders. Neither Mesa nor CTS is modified.
A broader hardware reset/recovery mechanism is still outstanding.

## Validation and retained limitations

The diagnostic module with the identical policy passes the isolated original
case, its original 68-case segment, and all 140 float16 logical graphics cases.
Evidence: `build/driver-roadmap-20260927/m3-ta-100`,
`m3-ta-100-original-batch`, and `m3-ta-policy-logical`.
The module SHA-256 is
`10d84a73b1d80894e78f539f9ec7492add8ec6fcd36d42cc1b295e17b7876d4e`.
Final-build deployment and post-reboot validation are recorded separately.

The earlier default-policy fault validated bounded `M3FWD001` devcoredump
collection: 478,763 bytes across 12 owned-RAM records, with individual sampling
intervals and SHA-256 hashes. The control region reports halt_count=1,
halted=1, resume=0. The snapshot intentionally excludes application buffers;
it is sequential evidence, not an atomic image. The first-fault capture code
has five offline parser/collection checks. This does not complete the audit of
all submission and teardown errors or prove autonomous GPU recovery.

Final development build `ad030b321e46`, module SHA-256
`d714d3112c3e953be7885e2cb0112d500bc9925b897b378117be41ff610e1222`,
passes all 140 logical cases again plus 88 rendering smoke cases after a normal
reboot with timing markers removed. Plasma is active and GPU health remains 1.
The publication adaptation `cc3a3b104f91` compiles separately and is pushed;
its larger historical source tree is not the deployed development module.
M3's missing full-catalog indices resumed with an explicit identity/review
record. Original outcomes, including the Crash, are preserved.
