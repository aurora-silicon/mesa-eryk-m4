#!/usr/bin/env python3
# Copyright 2026 Aurora Silicon
# SPDX-License-Identifier: MIT
"""Give staged D3D UMD DDI constants C internal linkage.

Microsoft's header declares 201 initialized namespace-scope const objects.
They have internal linkage for its normal C++ consumers but external linkage
in C.  The native UMD is C and has multiple DDI translation units, so the
staged copy needs the C spelling of Microsoft's intended linkage.
"""

import re
import sys
from pathlib import Path


PATTERN = re.compile(
    r"^const (UINT|INT|FLOAT) (D3D[A-Za-z0-9_]+\s*=)", re.MULTILINE
)


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} STAGED-d3d10umddi.h", file=sys.stderr)
        return 2

    path = Path(sys.argv[1]).resolve()
    expected_counts = {
        "d3d10umddi.h": 201,
        "d3d12umddi.h": 392,
    }
    expected = expected_counts.get(path.name.lower())
    if expected is None or "winddk" not in path.parts:
        print(f"refusing non-staged header: {path}", file=sys.stderr)
        return 2

    source = path.read_text(encoding="utf-8")
    rewritten, count = PATTERN.subn(r"static const \1 \2", source)
    if count != expected:
        print(
            f"expected {expected} WDK constants, found {count}; header pin changed",
            file=sys.stderr,
        )
        return 3

    path.write_text(rewritten, encoding="utf-8", newline="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
