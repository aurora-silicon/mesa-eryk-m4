#!/usr/bin/env python3
"""Static integration gate for native AGX sparse residency."""

from pathlib import Path


root = Path(__file__).resolve().parents[2]
private = (root / "d12_private.h").read_text()
resource = (root / "d12_resource.c").read_text()
command = (root / "d12_cmd.c").read_text()
queue = (root / "d12_queue.c").read_text()
dxil = (root / "dxil/d12_dxil_nir.c").read_text()
dxil_abi = (root / "dxil/d12_dxil.h").read_text()
descriptor = (root / "d12_descriptor.c").read_text()
device = (root / "d12_device.c").read_text()
draws = (root.parent / "libagx/draws.cl").read_text()
geometry = (root.parent / "libagx/geometry.cl").read_text()
sparse_shader = (root.parent / "libagx/d3d12_sparse.h").read_text()
sparse_core = (root / "d12_sparse_residency_core.h").read_text()
sparse_core_impl = (root / "d12_sparse_residency_core.c").read_text()
umd_resource_commands = (root / "umd/d12_umd_resource_command_funcs.c").read_text()


def function_body(text: str, name: str) -> str:
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


for token in (
    "struct agx_bo *sparse_map;",
    "struct agx_bo *residency_bo;",
    "D12_READ_REF_RESOURCE",
    "struct d12_sparse_map_update",
):
    assert token in private, f"sparse resource ABI omits {token}"

create = function_body(resource, "d12_resource_create(")
for token in (
    '"d3d12 sparse residency map"',
    "agx_bo_map(res->sparse_map)",
    "memset(map, 0, sparse_map_size_B)",
):
    assert token in create, f"reserved texture creation omits {token}"

commit = function_body(resource, "d12_sparse_commit_map_updates(")
for token in (
    "agx_bo_map(res->sparse_map)",
    "simple_mtx_lock(&res->sparse_lock)",
    "agx_pack(sparse_words + update->index_el, SPARSE_BLOCK, cfg)",
    "cfg.enabled = update->enabled",
    "cfg.address = update->read_address",
    "update->enabled ? UINT32_MAX : 0",
):
    assert token in commit, f"sparse map commit omits {token}"

for token in (
    "d12_sparse_texture_map_address(",
    "cfg.mode = AGX_IMAGE_MODE_SPARSE",
    "cfg.address = sparse_address",
):
    assert token in command, f"sparse image descriptor omits {token}"

read_resource = function_body(queue, "d12_queue_read_set_add_resource(")
for token in (
    "d12_queue_read_set_add_bo(op, resource->sparse_map)",
    "simple_mtx_lock(&resource->sparse_lock)",
    "resource->sparse_mappings[i]",
    "mapping.heap_offset_B",
    "d12_queue_read_set_add_bo_range(",
    "D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES",
):
    assert token in read_resource, f"sparse exact-read capture omits {token}"

write_resource = function_body(queue, "d12_queue_attachment_add_descriptor(")
for token in (
    "descriptor->resource->sparse_mappings[i]",
    "mapping.heap_offset_B",
    "d12_queue_attachment_add_bo_range(",
    "D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES",
    "AGX_SCRATCH_PAGE_ADDRESS",
):
    assert token in write_resource, f"sparse exact-write capture omits {token}"

descriptor_span = function_body(command, "d12_descriptor_resource_span(")
assert "d->kind == D12_DESC_SRV || d->kind == D12_DESC_CBV" in descriptor_span

for token in (
    "d12_sparse_buffer_size(",
    "d12_core_get_caps()->gpu_virtual_address_bits",
    "UINT64_MAX -",
):
    assert token in create, f"reserved size validation omits {token}"

bind = function_body(queue, "d12_queue_bind_sparse_ordered(")
wait = "d12_queue_wait_timeline(queue, in)"
mutate = "d12_sparse_commit_map_updates(resource, map_updates,"
vm_bind = "queue->device->dev.ops.bo_bind_timeline("
for token in (wait, mutate, vm_bind):
    assert token in bind, f"ordered sparse bind omits {token}"
assert bind.index(wait) < bind.index(mutate) < bind.index(vm_bind)

update = function_body(queue, "d12_queue_update_tile_mappings(")
ordering_lock = "d12_sync_lock(&queue->ddi_submit_lock)"
software_update = "d12_sparse_update_tile_mappings("
ordered_bind = "d12_queue_bind_sparse_ordered("
for token in (ordering_lock, software_update, ordered_bind):
    assert token in update, f"sparse mapping transaction omits {token}"
assert update.index(ordering_lock) < update.index(software_update) < update.index(
    ordered_bind
)

cbv = function_body(device, "d12_device_CreateConstantBufferView(")
for token in (
    "d12_sparse_buffer_for_address(device, desc->BufferLocation)",
    "d12_sparse_read_address(device, desc->BufferLocation)",
):
    assert token in cbv, f"descriptor-table CBV sparse translation omits {token}"

begin_render = function_body(command, "d12_list_begin_render(")
for token in (
    "dsv->resource->reserved",
    "dsv->resource->sparse_size_B",
):
    assert token in begin_render, f"reserved attachment validation omits {token}"

dsv = function_body(device, "d12_device_CreateDepthStencilView(")
for token in (
    "res->reserved",
    'CreateDepthStencilView: reserved depth resources are',
    "slot->resource = NULL",
):
    assert token in dsv, f"reserved DSV rejection omits {token}"

so = function_body(command, "d12_list_SOSetTargets(")
assert "d12_stream_output_view_valid(" in so

umd_so = function_body(
    umd_resource_commands, "d12_umd_set_stream_output_targets("
)
for token in (
    "if (!views[index].SizeInBytes)",
    "d12_stream_output_view_valid(",
    ".filled_size_address = views[index].BufferFilledSizeLocation",
):
    assert token in umd_so, f"DDI stream-output validation omits {token}"
assert umd_so.index("if (!views[index].SizeInBytes)") < umd_so.index(
    "d12_stream_output_view_valid("
)

draw = function_body(command, "d12_list_draw(")
write_tracking = draw
for token in (
    "d12_sparse_buffer_for_address(",
    "d12_list_track_read_resource(list, resource)",
):
    assert token in draw, f"reserved stream-output read tracking omits {token}"
for token in (
    "d12_sparse_buffer_for_address(",
    "d12_segment_track_write_resource(",
):
    assert token in write_tracking, (
        f"reserved stream-output write tracking omits {token}"
    )

worker = function_body(queue, "d12_queue_worker(")
worker_wait = "d12_queue_wait_timeline(queue, point)"
worker_mutate = "d12_sparse_commit_map_updates("
worker_bind = "queue->device->dev.ops.bo_bind("
for token in (worker_wait, worker_mutate, worker_bind):
    assert token in worker, f"worker sparse bind omits {token}"
assert worker.index(worker_wait) < worker.index(worker_mutate) < worker.index(
    worker_bind
)

for token in (
    "nir_is_sparse_texels_resident(b, 1, code)",
    "tex->is_sparse = true",
    "nir_tex_instr_dest_size(tex)",
    "dxil_sparse_feedback_status(b, nir_channel(b, &tex->def, 4))",
    "buffer_residency_pointer(",
    "buffer_residency_status(",
    "resource_base_address_read(",
    "D12_ROOT_PLANE_SPARSE_RO_OFFSET",
):
    assert token in dxil, f"DXIL sparse feedback omits {token}"

for token in (
    "D12_ROOT_RESIDENCY_BASE",
    "D12_ROOT_METADATA_HEADER_SIZE 56",
    "D12_SPARSE_BUFFER_TILE_SIZE 65536u",
):
    assert token in dxil_abi, f"root residency ABI omits {token}"

for token in (
    '"d3d12 bindless sparse residency"',
    "d12_descriptor_sparse_residency_address(slot)",
):
    assert token in descriptor, f"descriptor residency plane omits {token}"

descriptor_residency = function_body(
    command, "uint64_t\nd12_descriptor_sparse_residency_address("
)
for token in (
    "d12_sparse_buffer_residency_address(",
    "d12_sparse_texture_map_address(",
):
    assert token in descriptor_residency, (
        f"generic descriptor residency omits {token}"
    )

for token in (
    "d12_sparse_texture_access_index(",
    "offset_in_layer_B & ~(D12_AGX_SPARSE_PAGE_SIZE_B - 1)",
    "d12_sparse_residency_index(",
):
    assert token in sparse_core_impl, (
        f"texture atomic residency arithmetic omits {token}"
    )

for token in (
    "global uint64_t *out_residency",
    "constant uint64_t *previous_residency",
    "*out_residency = residency",
    "libagx_d3d12_sparse_read_address(",
):
    assert token in draws, f"indirect sparse residency patch omits {token}"

for token in (
    "struct d12_sparse_registry_view",
    "struct d12_sparse_registry_record",
    '_Static_assert(sizeof(struct d12_sparse_registry_view) == 16',
    '_Static_assert(sizeof(struct d12_sparse_registry_record) == 24',
):
    assert token in sparse_core, f"sparse registry ABI omits {token}"

for token in (
    "struct agx_bo *sparse_registry_bo;",
    "uint64_t sparse_registry_view_gpu;",
    "uint32_t sparse_registry_capacity;",
    "bool uses_sparse_registry;",
):
    assert token in private, f"command-list sparse snapshot omits {token}"

for token in (
    "struct libagx_d3d12_sparse_registry_view",
    "struct libagx_d3d12_sparse_registry_record",
    "while (low < high)",
    "address - record.base < record.size",
    "record.residency",
):
    assert token in sparse_shader, f"shader sparse registry omits {token}"

refresh = function_body(command, "d12_cmd_list_refresh_sparse_registry(")
for token in (
    "simple_mtx_lock(&device->sparse_resource_lock)",
    "d12_sparse_registry_allocation_size(",
    "agx_bo_create(",
    "qsort(records",
    "resource->sparse_map->va->addr",
    "*view = (struct d12_sparse_registry_view)",
    "__sync_synchronize()",
):
    assert token in refresh, f"submission-time sparse snapshot omits {token}"

execute_indirect = function_body(command, "d12_list_ExecuteIndirect(")
assert "d12_list_upload_sparse_ranges" not in command
guard = execute_indirect.index("if (has_gpu_addresses)")
assert execute_indirect.index(
    "sizeof(struct d12_sparse_registry_view)", guard
) > guard
assert execute_indirect.index("list->uses_sparse_registry = true", guard) > guard
assert "agx_bo_create(" not in execute_indirect

capture = function_body(queue, "d12_queue_op_capture_bos(")
assert "d12_queue_op_capture_bo(op, list->sparse_registry_bo)" in capture
submit = function_body(queue, "d12_queue_ExecuteCommandLists(")
assert submit.index("d12_cmd_list_refresh_sparse_registry(list)") < submit.index(
    "d12_queue_op_capture_bos(op, list)"
)

for function_name in (
    "libagx_d3d12_indirect_root(",
    "libagx_d3d12_indirect_vbv(",
    "libagx_d3d12_indirect_indexed_draw(",
):
    body = function_body(draws, function_name)
    assert "libagx_d3d12_sparse_read_address(" in body, (
        f"{function_name} omits execution-time sparse VA translation"
    )

for function_name in (
    "libagx_d3d12_unroll_restart_ibv(",
    "libagx_d3d12_gs_setup_indirect_ibv(",
):
    body = function_body(geometry, function_name)
    assert "libagx_d3d12_sparse_read_address(" in body, (
        f"{function_name} omits execution-time sparse VA translation"
    )

uav = function_body(device, "d12_device_CreateUnorderedAccessView(")
for token in (
    "!desc->Buffer.StructureByteStride",
    "cres->reserved",
    "D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT",
):
    assert token in uav, f"sparse UAV counter rejection omits {token}"

print(
    "sparse-residency: PASS "
    "(texture maps + buffer words + ordered mutation + DXIL feedback)"
)
