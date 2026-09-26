# Preserve compute memory effects in submission scheduling

The optional compute-to-tiler overlap check excludes predecessor streams
whose shaders can write memory. Native CDM dispatch did not set writes_memory,
and CDM merging did not propagate it. Thus compute producers (including meta
work) could be classified as side-effect-free. Record shader memory effects
when emitting the dispatch, conservatively include graphics lowered to compute,
and OR the effect when joining streams. The legacy raw-launch entry point lacks
shader metadata and is conservatively marked as a possible writer.

This fixes the missing scheduling metadata. It does not enable either optional
M4 compute-crossing or independent-tiling policy by default, and does not claim
a full Vulkan dependency-DAG implementation.

Satisfactory gameplay with both options, compact graphics descriptors and
native read-only preambles reproduced a GPU fault at job31206 (073823172470Z).
Peak 72 C, no thermal failure. Changing only the kernel to restore immediate
TA order reproduced the same failure at job374 (074827512214Z), peak 66.5 C.
Both dumps showed a shared pool with 576 populated pages, descriptor cursor
576, a matching manager/physical slot, and active TA/FS while compute waited.
Those observations alone do not establish the firmware-level failure mechanism.
The shared-pool run without compute-crossing passed (075000706858Z).

With the missing memory-effect tracking fixed, the formerly failing capture
passes on both that diagnostic kernel (075224275499Z) and the original
af4b29da29ba3cc1262617f7eb2ff25d429e4cef3920cdbde98ae301342eff3b
module (075524323082Z). The kernel experiment is reverted.

Final library SHA256:
d288e406b58c14f95eaa71943e0325e5acbf0f0657e1c20c33a20e814f04e25a

Final replay checks: M4 075810753133Z with both overlap options, M3
075833996510Z with normal scheduling. Frames 1,16,31 match every reference
RGB pixel at unchanged 3840x2160 output / 2880x1620 internal resolution.
Initial patch: ten compute CTS cases and eight synchronization2 cases pass.
Final patch, including conservative lowered-graphics handling: all eight
producer/consumer synchronization2 cases pass again (075953419935Z).
Selections retain frozen catalog indices and hash; results and guards are here.
Every hardware test had a verified 60-second watchdog and 0.5-second thermal
telemetry. Fault snapshots, exact binaries, source patches and all replay
records are retained in the private local benchmark repository. No Satisfactory
application launch or replacement of the normal desktop Mesa occurred.

This is a correctness fix, not a performance gain or a claim of whole-desktop
qualification for the experimental overlap policies.
