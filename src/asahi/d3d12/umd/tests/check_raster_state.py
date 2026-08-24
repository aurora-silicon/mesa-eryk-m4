#!/usr/bin/env python3
"""Static integration gate for native D3D12 rasterizer state."""

from pathlib import Path


d12 = Path(__file__).resolve().parents[2]
private = (d12 / "d12_private.h").read_text()
pipeline = (d12 / "d12_pipeline.c").read_text()
command = (d12 / "d12_cmd.c").read_text()
frontend = (d12 / "umd/d12_umd_pipeline_funcs.c").read_text()


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


assert "enum agx_polygon_mode polygon_mode;" in private

create = body(
    pipeline, "d12_pipeline_state_create_graphics(struct d12_device"
)
for token in (
    "pso->raster.polygon_mode = AGX_POLYGON_MODE_FILL",
    "d12_raster_polygon_mode(rs_desc->FillMode, &polygon_mode)",
    "pso->raster.polygon_mode = (enum agx_polygon_mode)polygon_mode",
):
    assert token in create, f"graphics PSO omits {token}"
assert "wireframe fill ignored" not in create

draw = body(command, "d12_list_draw(")
assert "cfg.polygon_mode = pso->raster.polygon_mode" in draw

translate = body(frontend, "d12_umd_create_rasterizer_state(")
assert ".fill_mode = (uint32_t)desc->FillMode" in translate

print("raster-state: PASS (native solid/wireframe polygon mode)")
