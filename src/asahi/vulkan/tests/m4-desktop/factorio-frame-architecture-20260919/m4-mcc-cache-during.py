#!/usr/bin/env python3
"""Sample native MCC DSID occupancy while a bounded checked child executes.

Run through gpu-experiment.py. Read-only MMIO: native H16G
getDSIDQuotaUseCount reads aperture+0x1cc000+dsid*4 for dsid0..191.
This records occupancy, not cache hits/misses or peak memory bandwidth.
"""
import json
import mmap
import os
from pathlib import Path
import subprocess
import sys
import time

out = Path(sys.argv[1])
command = sys.argv[2:]
guard = json.loads((out / 'start.json').read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds'] == 60
assert b'apple,t8132\0' in Path('/proc/device-tree/compatible').read_bytes()
assert command
fd = os.open('/dev/mem', os.O_RDONLY | os.O_SYNC | os.O_CLOEXEC)
maps = []
child = None
try:
    for base in (0x220000000, 0x222000000):
        maps.append(mmap.mmap(fd, mmap.PAGESIZE, flags=mmap.MAP_SHARED,
            prot=mmap.PROT_READ, offset=base + 0x1cc000))
    peak = [[0] * 192 for _ in maps]
    samples = 0
    with (out / 'dsid-occupancy.jsonl').open('w') as log, (out / 'child.log').open('w') as output:
        child = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 20
        while True:
            values = [[int.from_bytes(m[i*4:i*4+4], 'little') for i in range(192)] for m in maps]
            for a, row in enumerate(values):
                peak[a] = [max(old, value & 0xffff) for old, value in zip(peak[a], row)]
            log.write(json.dumps(dict(monotonic=time.monotonic(), raw_values=values)) + '\n')
            samples += 1
            if child.poll() is not None:
                break
            assert time.monotonic() < deadline, 'child exceeded local 20-second bound'
            time.sleep(.005)
        assert child.returncode == 0, child.returncode
    result = dict(command=command, samples=samples, peak_lines=peak,
        qualification='Occupancy only; no hit/miss or bandwidth claim')
    (out / 'dsid-occupancy-summary.json').write_text(json.dumps(result, indent=2) + '\n')
    for a, row in enumerate(peak):
        print('aperture', a, 'peak lines', {i:v for i,v in enumerate(row) if v})
    print((out / 'child.log').read_text())
    print('PASS: bounded MCC occupancy sampling and checked child')
finally:
    if child is not None and child.poll() is None:
        child.kill()
        child.wait(timeout=3)
    for m in maps:
        m.close()
    os.close(fd)
