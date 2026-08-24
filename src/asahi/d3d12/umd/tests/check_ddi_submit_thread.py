#!/usr/bin/env python3
"""Static integration gate for caller-thread DDI submission and KMD v4 waits."""

from pathlib import Path


root = Path(__file__).resolve().parents[2]
queue = (root / "d12_queue.c").read_text()
runtime = (root / "d12_device_runtime.c").read_text()
winsys = (root.parent / "lib/agx_device_d3dkmt.c").read_text()
winsys_private = (root.parent / "lib/agx_d3dkmt_priv.h").read_text()
winsys_core = (root.parent / "lib/agx_d3dkmt_core.c").read_text()
transport = (root / "umd/d12_umd_agx_transport.c").read_text()


def function(text: str, name: str, next_name: str) -> str:
    start = text.index(name)
    end = text.index(next_name, start + len(name))
    return text[start:end]


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


create = function(
    runtime,
    "d12_device_runtime_command_queue_create(",
    "d12_device_runtime_command_queue_destroy(",
)
assert "d12_queue_create_ddi(" in create

execute = function(
    queue,
    "d12_queue_ExecuteCommandLists(",
    "d12_queue_SetMarker(",
)
for token in (
    "queue->ddi_immediate_submit",
    "d12_sync_lock(&queue->ddi_submit_lock)",
    "d12_queue_take_pending_placement_waits(queue, op)",
    "mtx_unlock(&queue->lock)",
    "d12_queue_process_submit(queue, op)",
    "d12_queue_retire_enqueue(queue, op)",
):
    assert token in execute, f"caller-thread Execute path omits {token}"
assert execute.index("mtx_unlock(&queue->lock)") < execute.index(
    "d12_queue_process_submit(queue, op)"
)

signal = function(
    queue,
    "d12_queue_signal_placement_fence(",
    "d12_queue_wait_placement_fence(",
)
assert "d12_queue_process_submit(queue, op)" in signal
assert "d12_queue_enqueue_locked(queue, op)" in signal
assert signal.index("if (queue->ddi_immediate_submit)") < signal.index(
    "d12_queue_enqueue_locked(queue, op)"
)
assert "d12_queue_flush_pending_placement_waits_locked(queue)" in signal

wait = function(
    queue,
    "d12_queue_wait_placement_fence(",
    "d12_queue_GetTimestampFrequency(",
)
assert "pending_placement_waits" in wait
assert "d12_placement_fence_wait(" not in wait
assert "d12_queue_flush_pending_placement_waits_locked(queue)" in wait
assert wait.index("if (queue->ddi_immediate_submit)") < wait.index(
    "d12_queue_enqueue_locked(queue, op)"
)

submit = function(
    queue,
    "d12_queue_process_submit(",
    "d12_queue_wait_timeline(",
)
for token in (
    "AGX_SUBMIT_FENCE_WAIT",
    "AGX_SUBMIT_FENCE_SIGNAL",
    ".fence_dependency_count =",
    ".fence_dependencies =",
    "dev->ops.submit(dev, &slice, &virt)",
):
    assert token in submit, f"AGX submit path omits {token}"

for token in ("agx_d3dkmt_submit_private_pack(", "virt->fence_dependencies"):
    assert token in winsys, f"D3DKMT winsys omits {token}"
winsys_submit = function_body(winsys, "d3dkmt_submit(")
for token in ("malloc(", "calloc(", "free("):
    assert token not in winsys_submit, f"warm D3DKMT submit calls {token}"
assert "#define AGX_D3DKMT_STAGING_RING 64" in winsys_private
for token in (
    "d3dkmt_bo_bind_timeline(",
    ".hContext = ctx->hContext",
    ".hFenceObject = fence->kmt",
    ".FenceValue = wait_value",
    ".bo_bind_timeline = d3dkmt_bo_bind_timeline",
):
    assert token in winsys, f"queue-relative UpdateGpuVA omits {token}"
timeline_bind = function_body(winsys, "d3dkmt_bo_bind_timeline(")
assert ".Flags = 0" in timeline_bind
assert "DoNotWait must remain clear" in timeline_bind

sparse = function(
    queue,
    "d12_queue_bind_sparse_ordered(",
    "d12_queue_update_tile_mappings(",
)
for token in (
    "d12_queue_flush_pending_placement_waits_locked(queue)",
    "d12_queue_wait_timeline(queue, in)",
    "d12_sparse_commit_map_updates(resource, map_updates,",
    "queue->device->dev.ops.bo_bind_timeline(",
    "d12_queue_timeline_next_submit(",
    "d12_queue_retire_enqueue(queue, op)",
):
    assert token in sparse, f"DDI sparse queue interlock omits {token}"
assert sparse.index("d12_queue_wait_timeline(queue, in)") < sparse.index(
    "d12_sparse_commit_map_updates(resource, map_updates,"
)
assert sparse.index(
    "d12_sparse_commit_map_updates(resource, map_updates,"
) < sparse.index("queue->device->dev.ops.bo_bind_timeline(")
for token in (
    "AGXW_SUBMIT_PRIVATE_VERSION_V4",
    "struct agxw_submit_fence_dependency",
    "agxw_submit_private_validate(base, *out_size)",
):
    assert token in winsys_core, f"D3DKMT wire packer omits {token}"

callback = function(
    transport,
    "d12_umd_submit(",
    "d12_umd_create_sync(",
)
assert "pfnSubmitCommandCb(" in callback
assert "CreateThread" not in callback and "QueueUserWorkItem" not in callback

print(
    "ddi-submit-thread: PASS "
    "(caller thread + batched v4 waits + ordered sparse table/UpdateGpuVA)"
)
