# Satisfactory DX11 memory preamble with nopromote

The descriptor-prefetch comparison stopped before enabling prefetch: its
baseline combined native read-only memory preambles with AGX_MESA_DEBUG=nopromote.
Earlier exact preamble checks used ordinary constant promotion (empty compiler
flags). The new combination fails exact image comparison on both GPUs.
Original M3 maximum errors at frames 1/16/31: 90/56/36. Original M4: 0/15/50.
A warm M4 repetition changes 6,227/325,977/157,157 pixels relative to its first
run; it also fails the retained reference. Successful execution is not an
image pass. No timings were approved and no tolerance changed.

M3's arithmetic-only preamble control (memory hoisting disabled), with the same
nopromote flag, matches frame 1 exactly. Full ordinary nopromote without native
preambles had already matched all three reference frames on both GPUs during
cold guard qualification. The memory transform is being bisected, with an
independent cache-disabled M4 repeat to separate cache/state effects. This is
an open correctness issue, not a demonstrated descriptor-prefetch regression.
