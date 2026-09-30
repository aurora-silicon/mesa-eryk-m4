# Isolated Neo compiler experiment

Compiler sources imported from aurora-silicon/mesa agx3
259af84735247b697cd4148ccdcb1f43fdd5ad5f. Original copyrights retained.
This is an opt-in experiment, not a replacement for the qualified compiler.
The compiler is rebuilt against this checkout's common NIR definitions; its
own AGX IR and generated encoders are isolated by symbol namespacing. A field
adapter preserves the M3 frontend/launch contract and rejects unsupported
resource requirements. No kernel or device identity is spoofed.
