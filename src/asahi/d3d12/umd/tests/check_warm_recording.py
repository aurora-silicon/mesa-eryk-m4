#!/usr/bin/env python3
"""Keep warm command-list recording paths free of transient host allocation."""

from pathlib import Path
import re
import sys


def function_body(path: Path, name: str) -> str:
    source = path.read_text(encoding="utf-8")
    match = re.search(rf"(?m)^{re.escape(name)}\s*\(", source)
    if match is None:
        raise ValueError(f"{path}: function {name} not found")
    opening = source.find("{", match.end())
    if opening < 0:
        raise ValueError(f"{path}: function {name} has no body")

    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise ValueError(f"{path}: function {name} has an unterminated body")


def main() -> int:
    d12_dir = Path(__file__).resolve().parents[2]
    runtime = d12_dir / "d12_device_runtime.c"
    command = d12_dir / "d12_cmd.c"
    forbidden = re.compile(r"\b(?:calloc|malloc|realloc|HeapAlloc|free)\s*\(")
    failures: list[str] = []

    for path, name in (
        (runtime, "d12_device_runtime_command_list_resource_barriers"),
        (runtime, "d12_device_runtime_command_list_write_buffer_immediate"),
        (command, "d12_list_set_root_constants"),
    ):
        body = function_body(path, name)
        match = forbidden.search(body)
        if match is not None:
            failures.append(f"{path}:{name} calls {match.group(0).strip()}")

    begin_render = function_body(command, "d12_list_begin_render")
    for token in (
        "D12_SCISSORS_PER_SEGMENT",
        "D12_DEPTH_BIASES_PER_SEGMENT",
        "d12_viewport_plan_storage",
    ):
        if token in begin_render:
            failures.append(
                f"{command}:d12_list_begin_render eagerly references {token}"
            )

    lazy_arrays = function_body(command, "d12_segment_ensure_viewport_arrays")
    for token in ("d12_viewport_plan_storage", "d12_list_upload_zero"):
        if token not in lazy_arrays:
            failures.append(
                f"{command}:d12_segment_ensure_viewport_arrays lacks {token}"
            )

    execute_indirect = function_body(command, "d12_list_ExecuteIndirect")
    if "agx_bo_create(" in execute_indirect:
        failures.append(
            f"{command}:d12_list_ExecuteIndirect allocates a BO while recording"
        )

    sparse_refresh = function_body(
        command, "d12_cmd_list_refresh_sparse_registry"
    )
    if "agx_bo_create(" not in sparse_refresh:
        failures.append(
            f"{command}:sparse registry growth is not deferred to submission"
        )

    if failures:
        print("check_warm_recording.py: FAIL", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("check_warm_recording.py: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
