#!/usr/bin/env python3
"""Time one CTS case on x86 Linux or macOS; never bypass Linux M4 guards."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--case', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--icd', type=Path, default=Path('/usr/share/vulkan/icd.d/radeon_icd.json'))
p.add_argument('--library', type=Path, help='Explicit Vulkan loader library (required on macOS)')
p.add_argument('--vulkaninfo', default='vulkaninfo')
a = p.parse_args()
if platform.system() != 'Darwin' and platform.machine() != 'x86_64':
    p.error('Use the mandatory watchdog/thermal guarded runner on Linux M4')
if platform.system() == 'Darwin' and a.library is None:
    p.error('macOS comparisons require an explicit --library and --icd')
binary = a.binary.resolve(strict=True)
icd = a.icd.resolve(strict=True)
library = a.library.resolve(strict=True) if a.library else None
a.output.mkdir(parents=True, exist_ok=False)
out = a.output.resolve()
runtime = out / 'runtime'
runtime.mkdir(mode=0o700)
env = dict(os.environ, VK_DRIVER_FILES=str(icd), XDG_RUNTIME_DIR=str(runtime))
if platform.system() == 'Darwin':
    env['DYLD_LIBRARY_PATH'] = str(library.parent)
identity = subprocess.run([a.vulkaninfo, '--summary'], env=env, text=True,
                          capture_output=True, timeout=20)
(out/'vulkaninfo.txt').write_text(identity.stdout + identity.stderr)
identity.check_returncode()
qpa = out/'case.qpa'
command = [str(binary), '--deqp-case='+a.case, '--deqp-log-filename='+str(qpa),
           '--deqp-log-images=disable', '--deqp-log-shader-sources=disable']
if library:
    command.append('--deqp-vk-library-path='+str(library))
start = time.monotonic()
with (out/'stdout.txt').open('w') as log:
    try:
        result = subprocess.run(command, cwd=binary.parent, env=env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=60)
        exit_code, timed_out = result.returncode, False
    except subprocess.TimeoutExpired:
        exit_code, timed_out = None, True
wall = time.monotonic() - start
raw = qpa.read_text(errors='replace') if qpa.exists() else ''
status = re.findall(r'<Result StatusCode="([^"]+)"', raw)
duration = re.findall(r'<Number Name="TestDuration"[^>]*Unit="us">(\d+)</Number>', raw)
report = dict(host=platform.node(), platform=platform.platform(), case=a.case, command=command,
              binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
              icd=str(icd), wall_seconds=wall, timeout_seconds=60,
              timed_out=timed_out, exit_code=exit_code, statuses=status,
              case_seconds=[int(v)/1e6 for v in duration])
report['icd_json'] = json.loads(icd.read_text())
driver = Path(report['icd_json']['ICD']['library_path'])
driver = driver if driver.is_absolute() else icd.parent/driver
if driver.is_file():
    report['driver_sha256'] = hashlib.sha256(driver.read_bytes()).hexdigest()
if library:
    report['loader_sha256'] = hashlib.sha256(library.read_bytes()).hexdigest()
(out/'report.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
raise SystemExit(0 if not timed_out and exit_code == 0 and status == ['Pass'] else 1)
