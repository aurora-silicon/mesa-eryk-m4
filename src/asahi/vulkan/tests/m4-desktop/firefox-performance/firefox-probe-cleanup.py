#!/usr/bin/env python3
"""Remove closed, probe-owned profile copies after fetching their evidence.
Run on the target: firefox-probe-cleanup.py [--apply] EXPERIMENT_ID [...]
Never touches the source Firefox profile. Retains configuration hashes and the
cleanup manifest in each experiment; omit --apply for a preview.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--apply', action='store_true')
p.add_argument('runs', nargs='+')
a = p.parse_args()
base = Path('/home/e/.cache/m4-firefox-probes').resolve()
commands = []
for proc in Path('/proc').glob('[0-9]*'):
    try:
        commands.append((proc/'cmdline').read_bytes().split(b'\0'))
    except OSError:
        pass
for name in a.runs:
    assert re.fullmatch(r'[0-9]{8}T[0-9]{12}Z-[a-z0-9-]+', name), name
    evidence = Path('/root/asahi-evidence') / name / 'firefox'
    result = json.loads((evidence/'result.json').read_text())
    profile = Path(result['profile'])
    assert profile.parent == base and profile.name == name and not profile.is_symlink(), profile
    assert result.get('ended') is not None, 'Probe did not finish cleanup'
    assert not any(os.fsencode(str(profile)) in command for command in commands), 'Profile is in use'
    if not profile.exists():
        print(name, 'already absent')
        continue
    assert profile.resolve().parent == base
    files = [f for f in profile.rglob('*') if f.is_file() and not f.is_symlink()]
    report = dict(profile=str(profile), files=len(files), bytes=sum(f.stat().st_size for f in files),
                  configuration_sha256={f.name:hashlib.sha256(f.read_bytes()).hexdigest()
                                        for f in (profile/'prefs.js', profile/'user.js') if f.is_file()},
                  applied=a.apply)
    if a.apply:
        (evidence/'profile-cleanup.json').write_text(json.dumps(report, indent=2)+'\n')
        shutil.rmtree(profile)
    print(json.dumps(report))
