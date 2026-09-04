#!/usr/bin/env python3
"""Static wiring gate for the D3D12 frontend's complete retained/read set."""

from pathlib import Path


d12 = Path(__file__).resolve().parents[2]
cmd = (d12 / "d12_cmd.c").read_text()
queue = (d12 / "d12_queue.c").read_text()
descriptor = (d12 / "d12_descriptor.c").read_text()
private = (d12 / "d12_private.h").read_text()
virt = (d12.parent / "lib/agx_submit_virt.h").read_text()


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


assert "#define AGX_SUBMIT_MAX_READ_RANGES 256u" in virt
assert "struct d12_read_tracker reads;" in private
assert "uint64_t *live_resource_words;" in private

record = body(cmd, "d12_list_begin_recording(")
release = body(cmd, "d12_list_Release(")
assert "d12_list_read_tracker_reset(list)" in record
assert "d12_list_read_tracker_fini(list)" in release

precompiled = body(cmd, "d12_list_dispatch_precomp_to(")
for token in (
    "d12_list_track_read_bo(list, prog->bo)",
    "d12_list_mark_read_set_incomplete(list)",
):
    assert token in precompiled

precompiled_reads = body(cmd, "d12_list_track_precompiled_reads(")
for token in (
    "LIBAGX_D3D12_INDIRECT_PATCH",
    "libagx_d3d12_indirect_patch_args",
    "LIBAGX_D3D12_INDIRECT_CONSTANT",
    "libagx_d3d12_indirect_constant_args",
    "LIBAGX_D3D12_INDIRECT_DISPATCH",
    "libagx_d3d12_indirect_dispatch_args",
):
    assert token in precompiled_reads

resource = body(cmd, "d12_list_track_read_resource(")
assert "resource->reserved" in resource
assert "incomplete = true" in resource

dispatch = body(cmd, "d12_list_dispatch_pso(")
draw = body(cmd, "d12_list_draw(")
for function in (dispatch, draw):
    assert "d12_list_track_pso_reads(list, pso)" in function
    assert "d12_list_track_bound_reads(" in function
assert "d12_list_track_read_range(list, list->vb[slot].addr" in draw
assert "d12_list_track_read_range(list, list->ib.addr" in draw

resolve = body(queue, "d12_queue_read_set_resolve_range(")
assert "simple_mtx_lock(&dev->bo_map_lock)" in resolve
assert resolve.index("agx_bo_reference(bo)") < resolve.index(
    "simple_mtx_unlock(&dev->bo_map_lock)"
)

heap_update = body(descriptor, "d12_descriptor_heap_update_slot(")
for token in (
    "heap->live_resource_words[index >> 6]",
    "slot->address != device->null_va",
):
    assert token in heap_update

heap_reads = body(queue, "d12_queue_read_set_add_heap(")
for token in (
    "heap->live_resource_words",
    "u_bit_scan64(&active)",
    "simple_mtx_lock(&heap->device->descriptor_heap_lock)",
    "UINT64_MAX << (ref->heap.first & 63)",
    # A root range the signature sized explicitly must not declare the whole
    # tail of the heap: that is both the WDDM cache-maintenance cost and the
    # fastest route to the 256-range submit ceiling.
    "uint32_t limit = heap->desc.NumDescriptors;",
    "limit = ref->heap.first + ref->heap.count;",
    "w <= last_word",
    "index >= limit",
):
    assert token in heap_reads
assert "for (uint32_t i = ref->heap.first;" not in heap_reads
assert "w < heap->live_resource_word_count" not in heap_reads

heap_writes = body(queue, "d12_queue_collect_write_tracker(")
for token in (
    "heap->live_resource_words",
    "u_bit_scan64(&active)",
    "simple_mtx_lock(&heap->device->descriptor_heap_lock)",
    "UINT64_MAX << (ref->heap.first & 63)",
    "heap_limit = ref->heap.first + ref->heap.count;",
    "w <= last_word",
    "index >= heap_limit",
    "*complete = false",
):
    assert token in heap_writes
assert "for (uint32_t i = ref->heap.first;" not in heap_writes

indirect = body(cmd, "d12_list_ExecuteIndirect(")
assert "has_gpu_addresses" in indirect
guard = indirect.index("if (has_gpu_addresses)")
for token in (
    "sizeof(struct d12_sparse_registry_view)",
    "list->uses_sparse_registry = true",
    "d12_list_mark_read_set_incomplete(list)",
):
    assert indirect.index(token, guard) > guard

build = body(queue, "d12_queue_op_build_read_set(")
for token in (
    "complete = !list->reads.incomplete",
    "device->rodata_bo",
    "device->oclqry_bo",
    "AGX_ZERO_PAGE_ADDRESS",
    "AGX_SCRATCH_PAGE_ADDRESS",
    "dev->printf_buffer_address",
    "AGX_G17P_RENDER_CONTEXT_BASE",
    "op->read_set_complete = complete",
    "op->exact_read_range_count = 0",
):
    assert token in build, f"read-set builder omits {token}"

execute = body(queue, "d12_queue_ExecuteCommandLists(")
assert execute.index("d12_cmd_list_refresh_descriptors(list)") < execute.index(
    "d12_queue_op_build_read_set(op, list)"
)
assert execute.index(
    "d12_cmd_list_refresh_sparse_registry(list)"
) < execute.index("d12_queue_op_capture_bos(op, list)")
assert execute.index("d12_queue_op_capture_bos(op, list)") < execute.index(
    "d12_queue_op_build_read_set(op, list)"
)

capture = body(queue, "d12_queue_op_capture_bos(")
assert "d12_queue_op_capture_bo(op, list->sparse_registry_bo)" in capture

submit = body(queue, "d12_queue_process_submit(")
assert "AGX_SUBMIT_VIRT_READ_SET_COMPLETE" in submit
assert ".read_range_count = op->exact_read_range_count" in submit
assert "op->exact_read_ranges" in submit

print(
    "exact-read-set: PASS "
    "(post-refresh capture + live-heap bitmap + bounded canonical ranges)"
)
