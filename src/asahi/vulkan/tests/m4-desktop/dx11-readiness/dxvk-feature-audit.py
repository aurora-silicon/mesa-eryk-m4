#!/usr/bin/env python3
"""Audit pinned DXVK required features against a saved, single-device vulkaninfo.

This is a capability audit, not an execution or conformance test. Obtain M4
vulkaninfo with the mandatory watchdog/thermal guard and explicit driver ICD.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--dxvk", required=True, type=Path)
p.add_argument("--vulkaninfo", required=True, type=Path)
p.add_argument("--output", required=True, type=Path)
a = p.parse_args()
src_path = a.dxvk / "src/dxvk/dxvk_device_info.cpp"
src = src_path.read_text()
log = a.vulkaninfo.read_text()
devices = re.findall(r"^\s*deviceName\s*=\s*(.+)$", log, re.M)
if len(devices) != 1:
    p.error("vulkaninfo must describe exactly one device")
checks = []
required = re.findall(r"ENABLE_(?:EXT_)?FEATURE\(([^,]+),\s*(\w+),\s*true\)", src)
if not required:
    p.error("DXVK required-feature table was not recognized")
for group, name in required + [("D3D11 level/correctness", n) for n in (
        "drawIndirectFirstInstance", "tessellationShader", "logicOp",
        "vertexPipelineStoresAndAtomics", "robustImageAccess2", "transformFeedback", "geometryStreams")]:
    values = sorted(set(re.findall(r"^\s*" + re.escape(name) + r"\s*=\s*(\w+)", log, re.M)))
    checks.append(dict(group=group, name=name, values=values, passed=values == ["true"]))
header = (a.dxvk / "src/dxvk/dxvk_device_info.h").read_text()
vk_header = (a.dxvk / "include/vulkan/include/vulkan/vulkan_core.h").read_text()
for key in re.findall(r"ENABLE_EXT\((\w+),\s*true\)", src):
    macro = re.search(r"VkExtensionProperties\s+" + key + r"\s*=\s*vk::makeExtension\((\w+)\)", header)
    if not macro:
        p.error("unrecognized extension mapping: " + key)
    name = re.search(r"#define\s+" + macro[1] + r'\s+"([^"]+)"', vk_header)[1]
    present = bool(re.search(r"^\s*" + name + r"\s*:", log, re.M))
    checks.append(dict(group="extension", name=name, passed=present))
api = re.search(r"^\s*apiVersion\s*=\s*(\d+)\.(\d+)", log, re.M)
push = re.search(r"^\s*maxPushConstantsSize\s*=\s*(\d+)", log, re.M)
checks += [dict(group="limit", name="Vulkan >= 1.4", passed=bool(api and tuple(map(int, api.groups())) >= (1, 4))),
           dict(group="limit", name="push constants >= 256", passed=bool(push and int(push[1]) >= 256))]
report = dict(device=devices[0], dxvk_commit=subprocess.check_output(
    ["git", "-C", str(a.dxvk), "rev-parse", "HEAD"], text=True).strip(),
    inputs={str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in (src_path, a.vulkaninfo)},
    checks=checks, passed=all(c["passed"] for c in checks),
    limitation="Capability advertisement only; native DXVK and Steam execution must be tested separately.")
a.output.write_text(json.dumps(report, indent=2) + "\n")
print(f"{sum(c['passed'] for c in checks)}/{len(checks)} checks passed: {devices[0]}")
for c in checks:
    if not c["passed"]:
        print("MISSING:", c["name"])
raise SystemExit(0 if report["passed"] else 1)
