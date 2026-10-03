# Bounded early texture scheduling on native M3 fragments

`AGX_NATIVE_EARLY_TEXTURES=true` enables an experiment after the pressure
scheduler and before register allocation. It is false by default. The normal
register budget, instruction selection, texture-bank allocation and asynchronous
scoreboard assignment are unchanged.

The pressure schedule sometimes consumes one sample and then issues the next
independent sample into the same physical destination. A post-allocation pass
cannot overlap them. Moving the latter sample across independent SSA arithmetic
before allocation gives both results distinct lifetimes. The allocator accounts
for that increased demand normally; this is not a fixed register reservation.

Movement is limited to 32 intervening instructions within one basic block of a
main AGX3b fragment shader. Only eliminable, reorderable ALU without scheduling
side effects can be crossed. SSA producers, consumers and physical register
read/write overlaps stop movement. Memory operations, derivatives, coverage,
system-value reads, uniform publishers and other samples retain their order.
Vertex, compute, internal, secondary and helper/preamble shaders are excluded.

The option has separate Vulkan pipeline/shader UUID and Gallium disk-cache bits.
Four regression tests cover overlap of independent samples, SSA and physical
hazards, memory/coverage/uniform publication boundaries, shader exclusions and
the bounded window. All 326 compiler tests pass. All 32 retained Minecraft
fragment inputs compile in both modes; two binaries change, with unchanged
instruction count and scratch use but eight more register halves.

On the M3, both modes match the approved three-frame RGB reference exactly.
Two same-boot nine-pair studies with the unchanged 30-frame 3024x1837 Vulkan
capture give a combined median paired improvement of 0.519%, with 16 of 18
pairs positive. Separate medians are 176.74/177.76 and 176.55/177.56 replay FPS.
Final completion is included. The final header/cache bookkeeping rebuild has
fresh exact image checks and a further nine-pair repeat: 175.69/176.68 FPS,
median paired gain 0.635%, eight pairs positive. Its frozen 1,268-case texture
selection completes with 927 Pass, 341 NotSupported, zero Fail, timeouts or
device losses. Catalog and global case indices are retained, as are the
60-second watchdog receipts, 0.5-second temperature records and restored
kernel controls. This is focused qualification, not a full CTS claim.
These results do not establish native OpenGL or other-device qualification,
MacOS parity, or live Java FPS. The normal desktop build is unchanged.

Retained evidence: gpu-replay-bench/diagnostics/m3-macos-driver-gap-20261003/
early-textures. The matched own native archive from draw-attribution/native02
contains the actual descriptor of pipeline 110, used for 613 indirect world
API draws per measured frame. Its main fragment text has ten samples and shows
independent samples in adjacent instructions. Public archive compilation is not
proof that those exact PSO bytes were executed. The earlier pipeline-109 archive
has a different fragment variant and must not be treated as a byte-identical
comparison. Neither API draw counts nor code-size differences are per-shader
timing or proof of the remaining performance gap's cause.
