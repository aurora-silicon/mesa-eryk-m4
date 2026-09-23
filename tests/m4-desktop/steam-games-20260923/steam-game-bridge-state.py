#!/usr/bin/env python3
"""Record graphics bridge versions without credentials or complete environments."""
import hashlib,json,os
from pathlib import Path
files=['/home/e/.local/share/m4-fex/mesa/lib/libvulkan_asahi.so','/home/e/.local/share/m4-fex/mesa/lib/libgallium-26.1.4.so','/home/e/.local/share/m4-fex/thunks/host/libvulkan-host.so','/usr/bin/FEXInterpreter']
print(json.dumps({'kernel':os.uname().release,'files':{p:hashlib.file_digest(Path(p).open('rb'),'sha256').hexdigest() for p in files},'mount_dropin':Path('/etc/systemd/system/home-e-.local-share-m4\\x2dfex-mesa.mount.d/50-shared-mesa.conf').read_text()},indent=2))
