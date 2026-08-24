#!/usr/bin/env python3
"""Static safety gate for DXIL resource-declaration storage."""

from pathlib import Path


d12 = Path(__file__).resolve().parents[2]
header = (d12 / "dxil/d12_dxil.h").read_text()
module = (d12 / "dxil/d12_dxil_module.c").read_text()
pipeline = (d12 / "d12_pipeline.c").read_text()
nir = (d12 / "dxil/d12_dxil_nir.c").read_text()


assert "struct d12_resource_binding *resources;" in header
assert "unsigned resource_capacity;" in header
assert "resources[D12_MAX_MODULE_RESOURCES]" not in header
assert "#define D12_MAX_MODULE_RESOURCES" not in header

for token in (
    "mod->resource_capacity >= D12_MAX_METADATA",
    "capacity = MIN2(capacity, D12_MAX_METADATA)",
    "realloc(",
    "out of memory growing DXIL resource declarations",
    "free(mod->resources)",
):
    assert token in module

assert "if (mod->num_resources >= D12_MAX_MODULE_RESOURCES)" not in module
assert pipeline.count("d12_dxil_module_finish(&mod);") >= 4

assert "dynamic resource binding index unsupported" not in nir
for token in (
    "unsigned klass, lower, upper, space",
    "unbounded_root_for_register(",
    "finite_root_range_count(ctx->map, klass, space, lower",
    "ctx->handle_heap[inst->result_value] = 3",
    "unsigned reg = rb->lower_bound + index",
    "b, slot_def, rb->lower_bound - rooted_reg",
    "b, slot, lower - rooted_reg",
):
    assert token in nir
assert "if (reg < rb->lower_bound)" not in nir
assert "nir_ult(b, index, nir_imm_int(b, lower))" not in nir

# A rooted range that the root signature sized explicitly ends there. Both
# shader translators must share one extent-aware resolver: a register past
# the end belongs to another root parameter, and resolving it against this
# base reads an unrelated descriptor out of the heap.
dxbc = (d12 / "dxil/d12_dxbc.c").read_text()
root_sig = (d12 / "d12_root_sig.c").read_text()
for token in (
    "uint32_t range_count;",
    "d12_binding_map_rooted_base(",
    "reg - map->entries[i].reg >= map->entries[i].range_count",
    "d12_binding_map_add_rooted_visible(",
    "bool overflow;",
):
    assert token in header, token
for translator in (nir, dxbc):
    assert "d12_binding_map_rooted_base(map, klass, space, reg" in translator
    assert "best_reg = map->entries[i].reg;" not in translator
assert "range->num_descriptors" in root_sig
assert "d12_binding_map_add_rooted_visible(" in root_sig

# A legal signature whose sized ranges do not fit the dense table must be
# rooted harder rather than refused, and the default expansion must stay the
# fast path so nothing that already fits changes shape.
assert "range->num_descriptors >= rooting_threshold" in root_sig
assert "D12_MAX_RESOURCE_BINDINGS, 64, 16, 4, 1," in root_sig
assert "d12_root_layout_build_map_rooting_at(layout, out, thresholds[i])" in root_sig

# Dense-slot exhaustion must be reported, not silently dropped.
assert "map->overflow = true;" in header
assert "bool d12_root_layout_build_map(" in (d12 / "d12_private.h").read_text()
assert "return !out->overflow;" in root_sig
for consumer in (pipeline, (d12 / "d12_device.c").read_text()):
    assert "if (!d12_root_layout_build_map(" in consumer

print(
    "resource-binding: PASS "
    "(dynamic declarations + bounded growth + SM 6.6 dynamic ranges "
    "+ rooted range extents + reported slot exhaustion)"
)
