# Native F32 FMA with independently sized inputs

Native AGX3B FMA now consumes each F16 register source directly when its result
is F32 and unsaturated. The optimizer folds the widening conversion into the
instruction. The encoder retains independent source widths, abs/neg modifiers,
lifetimes and waits. Narrow-result and saturated legalization is unchanged.

Nine byte fixtures from our Metal compiler witnesses cover all eight source
width combinations and odd half registers. Invalid widths, register alignment,
cache/discard conflicts and wait masks are rejected. All 384 existing modifier
fixtures and 297 compiler tests pass.

`validation/mixed-fma-oracle.py` generates 2,097,152 FMA results: each half input
traverses all 65,536 encodings, across eight width and four modifier combinations.
Explicit NoContraction decorations retain the FMA instructions. Both M3 and M4
match the host IEEE fmaf reference exactly, canonicalizing only NaN payloads;
signed zero is checked. Full-width inputs are normals or signed zero, so this is
not a qualification of arbitrary F32 denormal behavior. Retained AIR confirms
that the hardware probe actually uses half-source FMA instructions.

An earlier GLSL-only probe allowed multiply/add splitting and common-subexpression
reuse. Its outputs all matched the independent two-rounding model, but that run
does not qualify the new encoding. One SPIR-V 1.5 assembly with legacy BufferBlock
was rejected before dispatch; the qualified generator assembles and validates
for Vulkan 1.1. These excluded attempts and recovery evidence are retained locally.

Qualified library SHA256:
549812aa922a6166b1fb9e12633808c78cf5a8cf70201f23206439ada1e6f177.
Strict probe SHA256: 2757d38b828e0296e41cebe162a5621de381f75530446fc75d28133bd05b7340.
Expected buffer SHA256: 88e2faed14d994d2c9cc3e1b785709c7b634d158f27a8ca0566ca928d2628bb0.
Six pinned highp/mediump and fp16-storage FMA CTS cases pass on each GPU.

The unchanged Satisfactory Vulkan gameplay replay matches RGB frames 1, 16 and
31 exactly on both GPUs (M3 check 20260926T013720853431Z; M4 check
20260926T013920420373Z). One M3 timing completed 30 frames in 6.591307659 seconds,
4.55145 replay FPS: no measurable gain on its own. The change provides qualified
mixed input instructions for subsequent compiler work. Every hardware run used
the watchdog and thermal guards; normal desktop installations were unchanged.
