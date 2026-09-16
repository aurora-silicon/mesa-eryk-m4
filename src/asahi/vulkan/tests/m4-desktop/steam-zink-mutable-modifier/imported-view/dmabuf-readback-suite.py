#!/usr/bin/env python3
"""Run the imported-view regression and ordinary/MSAA controls under a GPU guard.

Usage: dmabuf-readback-suite.py /absolute/path/to/dmabuf-readback-probe
The caller supplies the native Mesa environment and Vulkan validation layer.
"""
import os
from pathlib import Path
import subprocess
import sys

assert os.environ.get('M4_TEST_CPU_MAX_KHZ'), 'mandatory GPU guard missing'
probe = Path(sys.argv[1])
assert probe.is_absolute() and probe.is_file()
for modifier in ('0', '0x0c00000000000001'):
    for samples in ('1', '4'):
        for mode in ('texture', 'texture-view'):
            print('CASE', modifier, samples, mode, flush=True)
            subprocess.run([str(probe), modifier, samples, mode], check=True, timeout=15)
print('PASS: eight DMA-BUF readback configurations', flush=True)
