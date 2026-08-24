#!/usr/bin/env python3
"""Static integration gate for D3D12 MINIMUM/MAXIMUM filter reduction.

AGX has no hardware reduction mode, so a MINIMUM/MAXIMUM sampler is reduced in
the shader.  Silently dropping the reduction returns a filtered average where
the API promised an extremum, which no test of the standard filter can catch --
these assertions keep the pieces of that path wired together.
"""

from pathlib import Path


d12 = Path(__file__).resolve().parents[2]
private = (d12 / "d12_private.h").read_text()
device = (d12 / "d12_device.c").read_text()
pipeline = (d12 / "d12_pipeline.c").read_text()
core = (d12 / "d12_sampler_reduction_core.h").read_text()
command = (d12 / "d12_cmd.c").read_text()


def body(text: str, name: str) -> str:
    start = text.index(name)
    opening = text.index("{", start + len(name))
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening : index + 1]
    raise AssertionError(f"unterminated function {name}")


# The reduction travels in the same per-sampler flag word as the other state
# AGX keeps outside its packed descriptor.
for token in (
    "#define D12_SAMPLER_REDUCTION_MIN",
    "#define D12_SAMPLER_REDUCTION_MAX",
    "#define D12_SAMPLER_REDUCTION_MASK",
):
    assert token in private, token

# Policy lives in the portable core, not open-coded at the packer.
for token in (
    "d12_sampler_reduction_for_filter",
    "D12_FILTER_REDUCTION_MINIMUM",
    "D12_FILTER_REDUCTION_MAXIMUM",
    "force_mip_nearest",
):
    assert token in core, token

pack = body(device, "d12_pack_sampler_descriptor(")
for token in (
    "d12_sampler_reduction_for_filter((uint32_t)desc->Filter)",
    "reduction_policy.force_mip_nearest",
    "D12_SAMPLER_REDUCTION_MIN",
    "D12_SAMPLER_REDUCTION_MAX",
):
    assert token in pack, f"sampler packer omits {token}"

# A reducing sampler must select a single mip level, or the gather footprint
# and the hardware's own level selection disagree.
assert "cfg.mip_filter = !reduction_policy.force_mip_nearest &&" in pack

lower = body(pipeline, "d12_lower_sampler_reduction(")
for token in (
    "nir_tex_instr_is_query(tex)",
    "tex->is_shadow",
    "d12_reduction_gatherable(tex)",
    "nir_type_float",
    "tex->def.num_components - (tex->is_sparse ? 1 : 0)",
    "d12_sampler_sideband_entry(b, tex, &bindless, &entry)",
    "D12_SAMPLER_REDUCTION_MASK",
    "D12_SAMPLER_REDUCTION_MIN",
    "nir_push_if(b, reduces)",
    "d12_reduction_gather(b, tex, c)",
    "nir_fmin(",
    "nir_fmax(",
    "nir_bcsel(b, is_min, minimum, maximum)",
    "nir_if_phi(b, reduced, sampled)",
):
    assert token in lower, f"reduction lowering omits {token}"

gather = body(pipeline, "d12_reduction_gather(")
assert "gather->op = nir_texop_tg4;" in gather
assert "gather->component = component;" in gather
# A gather takes no LOD, bias or gradient; carrying one over would build an
# instruction the backend cannot encode.
for forbidden in (
    "nir_tex_src_lod",
    "nir_tex_src_bias",
    "nir_tex_src_ddx",
    "nir_tex_src_ddy",
    "nir_tex_src_comparator",
):
    assert forbidden not in gather, f"gather must not carry {forbidden}"

gatherable = body(pipeline, "d12_reduction_gatherable(")
assert "GLSL_SAMPLER_DIM_2D" in gatherable
assert "GLSL_SAMPLER_DIM_CUBE" in gatherable
assert "GLSL_SAMPLER_DIM_1D" not in gatherable
assert "GLSL_SAMPLER_DIM_3D" not in gatherable

# Wired into both the compute and the graphics pipeline, before the sampler
# handles are rewritten to native indices.
assert pipeline.count("NIR_PASS(_, nir, d12_nir_lower_sampler_reduction);") == 2
for text in (pipeline,):
    reduction_at = text.index("NIR_PASS(_, nir, d12_nir_lower_sampler_reduction);")
    remap_at = text.index("NIR_PASS(_, nir, d12_nir_lower_bindless_sampler_map);")
    assert reduction_at < remap_at, (
        "reduction lowering must run while sampler handles are still logical"
    )

# The non-bindless side table carries the same flag word.
assert "entries[logical].flags = chosen->sampler_reserved;" in command

print(
    "sampler-reduction: PASS "
    "(policy core + descriptor flags + single-level gather reduction)"
)
