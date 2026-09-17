#!/usr/bin/env python3
"""Bounded process/kernel stack sampling around an already guarded CTS batch."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--guard-output', type=Path, required=True)
p.add_argument('command', nargs=argparse.REMAINDER)
a = p.parse_args()
spec = importlib.util.spec_from_file_location('fw', '/root/firmware-snapshot.py')
fw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fw)
boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
out = a.guard_output/'profile'
out.mkdir()
command = a.command[1:] if a.command[:1] == ['--'] else a.command
fw.check_guard(a.guard_output, boot)
proc = subprocess.Popen(command)
begin = time.monotonic()
stacks = set()
try:
    with (out/'samples.jsonl').open('w') as log:
        while proc.poll() is None:
            fw.check_guard(a.guard_output, boot)
            assert time.monotonic()-begin < 30
            for entry in Path('/proc').glob('[0-9]*'):
                pid = int(entry.name)
                try:
                    if not Path(f'/proc/{pid}/exe').resolve().name.startswith('deqp-vk'):
                        continue
                    if str(a.guard_output).encode() not in (entry/'cmdline').read_bytes():
                        continue
                    row = dict(seconds=time.monotonic()-begin,pid=pid)
                    for name in ['stat','wchan','stack','syscall']:
                        try:
                            row[name] = Path(f'/proc/{pid}/{name}').read_text()
                        except OSError as e:
                            row[name] = str(e)
                    log.write(json.dumps(row)+'\n');log.flush()
                    if pid not in stacks and row['seconds'] > 3:
                        stacks.add(pid)
                        with (out/f'{pid}-stack.txt').open('w') as stack:
                            try:
                                subprocess.run(['eu-stack','-p',str(pid)],stdout=stack,stderr=stack,timeout=3)
                            except subprocess.TimeoutExpired:
                                stack.write('stack sampling timed out\n')
                except OSError:
                    pass
            time.sleep(.5)
finally:
    if proc.poll() is None:
        from cts_deqp_backend import kill_tree
        kill_tree(proc)
raise SystemExit(proc.returncode)
