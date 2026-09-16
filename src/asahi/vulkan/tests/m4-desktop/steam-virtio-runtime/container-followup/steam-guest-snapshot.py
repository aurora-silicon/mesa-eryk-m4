#!/usr/bin/env python3
"""Read-only guest process evidence; run in an already guarded Steam VM.

Usage: muvm -i -- python3 /path/to/steam-guest-snapshot.py OUTPUT.json
OUTPUT must be a writable shared-home path. Does not launch GPU work.
"""
import json
from pathlib import Path
import sys
import time

processes = []
for proc in Path('/proc').glob('[0-9]*'):
    try:
        command = (proc / 'cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
        if not command:
            continue
        record = dict(pid=int(proc.name), command=command,
                      status=(proc / 'status').read_text(),
                      wchan=(proc / 'wchan').read_text())
        if any(name in command for name in ('steamwebhelper', '/steam ', 'portal')):
            record['maps'] = (proc / 'maps').read_text()
            record['threads'] = []
            for task in (proc / 'task').glob('[0-9]*'):
                try:
                    record['threads'].append(dict(tid=int(task.name),
                        name=(task / 'comm').read_text().strip(),
                        wchan=(task / 'wchan').read_text()))
                except OSError:
                    pass
        processes.append(record)
    except OSError:
        pass
Path(sys.argv[1]).write_text(json.dumps(dict(time=time.time(), processes=processes), indent=2)+'\n')
