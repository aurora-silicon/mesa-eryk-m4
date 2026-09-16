#!/usr/bin/env python3
"""Bounded native syscall trace of CEF sandbox children in a guarded VM.

Run with muvm --privileged -i inside an existing experiment. Writes OUTPUT.
Waits up to 40 seconds for the sandbox zygote, then traces it for 15 seconds.
"""
import os
from pathlib import Path
import subprocess
import sys
import time

assert os.getuid() == 0 and os.sysconf('SC_PAGE_SIZE') == 4096
deadline = time.monotonic() + 40
while time.monotonic() < deadline:
    pids = []
    for proc in Path('/proc').glob('[0-9]*'):
        try:
            command = (proc / 'cmdline').read_bytes()
            if b'steamwebhelper' in command and b'--type=zygote' in command and b'--no-zygote-sandbox' not in command:
                pids.append(proc.name)
        except OSError:
            pass
    if len(pids) >= 2:
        break
    time.sleep(.2)
else:
    raise SystemExit('No sandbox zygote found')
print('Tracing sandbox zygotes:', pids, flush=True)
command = ['/usr/bin/strace', '-f', '-tt', '-e',
           'trace=clone,clone3,mmap,mprotect,prlimit64,setrlimit,seccomp,prctl',
           '-o', sys.argv[1]]
for pid in pids:
    command += ['-p', pid]
child = subprocess.Popen(command)
try:
    child.wait(timeout=15)
except subprocess.TimeoutExpired:
    child.terminate()
    child.wait(timeout=3)
