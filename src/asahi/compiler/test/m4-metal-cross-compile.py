#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile fs/vs Metal probes to G16G binaries on a macOS compiler host.

This performs offline compilation only. The host GPU is never used and the
result does not establish successful execution on M4 hardware.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("sources", nargs="+", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sources = [source.resolve(strict=True) for source in args.sources]
    if len({source.stem for source in sources}) != len(sources):
        parser.error("source basenames must be unique")

    commands = []

    def run(command):
        result = subprocess.run(command, cwd=output, text=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=120)
        commands.append(dict(command=command, status=result.returncode,
                             output=result.stdout))
        (output / "commands.json").write_text(json.dumps(commands, indent=2))
        result.check_returncode()
        return result.stdout.strip()

    metadata = {
        "target": "applegpu_g16g",
        "hardware_execution": False,
        "host": run(["sw_vers"]),
        "compiler": run(["xcrun", "applegpu-nt", "--version"]),
        "compiler_path": run(["xcrun", "--find", "applegpu-nt"]),
        "sdk_version": run(["xcrun", "--sdk", "macosx", "--show-sdk-version"]),
        "probes": [],
    }
    architectures = run(["xcrun", "applegpu-nt", "-archs"]).split()
    if metadata["target"] not in architectures:
        raise RuntimeError("this toolchain does not list the G16G target")

    for source in sources:
        staged = output / source.name
        if staged != source:
            shutil.copyfile(source, staged)
        library = source.stem + ".metallib"
        script = source.stem + ".mtlp-json"
        archive = source.stem + ".archive"
        pipeline = {
            "libraries": {"paths": [{"label": "probe", "path": "./" + library}]},
            "pipelines": {"render_pipelines": [{
                "vertex_function": "alias:probe#vs",
                "fragment_function": "alias:probe#fs",
                "color_attachments": [{"pixel_format": "RGBA8Unorm"}],
            }]},
        }
        (output / script).write_text(json.dumps(pipeline, indent=2))
        run(["xcrun", "metal", staged.name, "-o", library])
        run(["xcrun", "applegpu-nt", "-arch", metadata["target"],
             "-platform_version", "macos", "26.0", metadata["sdk_version"],
             library, "-N", script, "-o", archive])
        metadata["probes"].append({
            "source": staged.name, "source_sha256": sha256(staged),
            "archive": archive, "archive_sha256": sha256(output / archive),
            "archive_bytes": (output / archive).stat().st_size,
        })
        (output / "compiled.json").write_text(json.dumps(metadata, indent=2))
        print(archive, "compiled for", metadata["target"], flush=True)


if __name__ == "__main__":
    main()
