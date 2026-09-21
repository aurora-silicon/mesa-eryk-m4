# Opt-in M3 Gen9 render compression

Admit G15S to the existing G16G RGBA8/BGRA8 UNORM 2D compression path,
including its matching exact-format copy handling and unchanged eligibility
restrictions. Defaults stay disabled; array/3D/MSAA allocations remain excluded.
The one-layer/one-mip restriction now explicitly applies to both chips.

J514S adapted STK replay allocates six compressed targets (three944x944 and
three1261x1544); output is pixel-identical to the same integer-preamble/UBO
variant without compression. The real3024x1964 racing sample gives33.10FPS
versus32.70FPS without it; this small scene-dependent difference establishes
no useful isolated performance gain. Peak70.25C. Keep compression optional.
Broader CTS qualification is separate. All hardware work uses the M3 guard.
