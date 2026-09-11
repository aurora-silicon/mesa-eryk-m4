#!/usr/bin/env python3
import hashlib, pathlib, sys
h=hashlib.sha256()
for p in sys.argv[2:]:
    data=pathlib.Path(p).read_bytes()
    h.update(len(data).to_bytes(8, 'little')); h.update(data)
pathlib.Path(sys.argv[1]).write_text('#define AGX_REPLAY_ABI "'+h.hexdigest()+'"\n')
