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
tests pass, and both host and ARM Vulkan builds pass. Hardware checks are pending.

Private input binaries remain in gpu-replay-bench. The adjacent profile records
input hashes, private provenance paths and before/after compiler statistics.
