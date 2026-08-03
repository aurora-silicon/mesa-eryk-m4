# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
#
# Measure the DXIL front end against a large corpus of real shaders.
#
# Our coverage estimate has so far come from five shaders we wrote ourselves,
# which says nothing about what real HLSL does. vkd3d-proton's test suite ships
# hundreds of DXC-compiled DXIL blobs embedded as C byte arrays; running every
# one through our front end turns a guess into a measurement, and re-prices the
# whole work queue.
#
# The blobs are consumed as test INPUT. No vkd3d code is copied.
#
# Usage: run_dxil_corpus.py <vkd3d-tests-dir> <compile-tool> [workdir]

import os
import re
import subprocess
import sys
from collections import Counter

ARRAY = re.compile(
    r"static\s+const\s+BYTE\s+(?P<name>[A-Za-z0-9_]+_code_dxil)\s*\[\s*\]\s*=\s*\{(?P<body>.*?)\};",
    re.S,
)
BYTE = re.compile(r"0x([0-9a-fA-F]{2})")

# Map a failure message to a bucket we can act on.
BUCKETS = [
    ("multi-block control flow", "multi-block"),
    ("unhandled dx.op", "unhandled dx.op intrinsic"),
    ("unhandled LLVM instruction", "unhandled LLVM instruction"),
    ("non-constant resource handle", "dynamic resource handle"),
    ("dynamic resource range", "dynamic resource range"),
    ("call to a non-dx.op", "non-dx.op call (needs inlining)"),
    ("unhandled compare predicate", "compare predicate"),
    ("unhandled LLVM cast", "LLVM cast"),
    ("unhandled LLVM binop", "LLVM binop"),
    ("sparse buffer store", "sparse write mask"),
    ("dynamic component index", "dynamic component index"),
    ("out-of-range value", "value resolution"),
    ("too few arguments", "malformed/unsupported call shape"),
    ("no function definition", "no entry point found"),
    ("module parse failed", "MODULE PARSE"),
    ("bitcode", "CONTAINER/BITSTREAM"),
]


def bucket_for(text):
    low = text.lower()
    for needle, name in BUCKETS:
        if needle.lower() in low:
            return name
    return "other: " + text.strip().splitlines()[0][:60] if text.strip() else "unknown"


def extract(tests_dir):
    """Yield (name, bytes) for every *_code_dxil array in the corpus."""
    for root, _dirs, files in os.walk(tests_dir):
        for fn in files:
            if not fn.endswith(".h"):
                continue
            path = os.path.join(root, fn)
            try:
                with open(path, "r", errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            for m in ARRAY.finditer(text):
                data = bytes(int(b, 16) for b in BYTE.findall(m.group("body")))
                if data:
                    yield m.group("name"), data


def main():
    tests_dir, tool = sys.argv[1], sys.argv[2]
    workdir = sys.argv[3] if len(sys.argv) > 3 else "/tmp/dxil_corpus"
    os.makedirs(workdir, exist_ok=True)

    results = Counter()
    failures = Counter()
    stage_names = []
    total = 0

    for name, data in extract(tests_dir):
        total += 1
        path = os.path.join(workdir, name + ".dxil")
        with open(path, "wb") as f:
            f.write(data)

        try:
            p = subprocess.run(
                [tool, path], capture_output=True, text=True, timeout=60
            )
            out = p.stdout + p.stderr
        except subprocess.TimeoutExpired:
            results["timeout"] += 1
            failures["timeout"] += 1
            continue
        except Exception as e:  # tool missing etc: fatal, not a data point
            print(f"FATAL: cannot run {tool}: {e}", file=sys.stderr)
            return 2

        # How far did it get?
        if "NIR->AGX OK" in out:
            results["compiled to AGX"] += 1
            m = re.search(r"(\w+) shader,", out)
            if m:
                stage_names.append(m.group(1))
        elif "DXIL->NIR OK" in out:
            results["translated, AGX compile failed"] += 1
            failures["AGX backend: " + bucket_for(out)] += 1
        elif "DXIL->NIR failed" in out:
            results["parsed, translation failed"] += 1
            failures[bucket_for(out)] += 1
        elif "module parse failed" in out:
            results["module parse failed"] += 1
            failures["MODULE PARSE: " + bucket_for(out)] += 1
        else:
            results["container/bitstream failed"] += 1
            failures["CONTAINER: " + bucket_for(out)] += 1

    print("=" * 68)
    print(f"DXIL corpus: {total} real DXC-produced shaders from vkd3d-proton")
    print("=" * 68)
    print()
    print("OUTCOME")
    for k in [
        "compiled to AGX",
        "translated, AGX compile failed",
        "parsed, translation failed",
        "module parse failed",
        "container/bitstream failed",
        "timeout",
    ]:
        n = results.get(k, 0)
        if n:
            print(f"  {n:5d}  {100.0*n/total:5.1f}%   {k}")
    print()

    ok = results.get("compiled to AGX", 0)
    parsed = total - results.get("container/bitstream failed", 0) \
                   - results.get("module parse failed", 0)
    print(f"  front end reaches the module decoder on {parsed}/{total} "
          f"({100.0*parsed/total:.1f}%)")
    print(f"  full DXIL -> AGX on {ok}/{total} ({100.0*ok/total:.1f}%)")
    if stage_names:
        print(f"  compiled by stage: {dict(Counter(stage_names))}")
    print()

    print("BLOCKERS, most frequent first (this is the work queue)")
    for cause, n in failures.most_common(20):
        print(f"  {n:5d}  {100.0*n/total:5.1f}%   {cause}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
