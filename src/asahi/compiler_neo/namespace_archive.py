#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Isolate the imported compiler's definitions, preserving external Mesa APIs."""
from pathlib import Path
import subprocess
import sys
import tempfile

source, output = sys.argv[1:]
nm = subprocess.check_output(['llvm-nm', '--defined-only', '--extern-only',
                              '--format=posix', source], text=True)
symbols = set()
for line in nm.splitlines():
    fields = line.split()
    if len(fields) >= 3 and len(fields[1]) == 1 and fields[1].isalpha():
        if fields[0] != 'agx_neo_compile_bridge':
            symbols.add(fields[0])
assert 'agx_compile_shader_nir' in symbols
with tempfile.TemporaryDirectory() as temp:
    mapping = Path(temp)/'symbols.txt'
    mapping.write_text(''.join(f'{s} neo_{s}\n' for s in sorted(symbols)))
    subprocess.run(['llvm-objcopy', '--redefine-syms='+str(mapping), source, output], check=True)
