#!/usr/bin/env python3
"""Target-only: add 4 GiB Btrfs swap after an observed host OOM.

Run through gpu-experiment.py. Uses a separate subvolume, preserves fstab,
formats for the running kernel's page size, and verifies activation before
making it persistent. Does not change GPU or application graphics settings.
"""
import os
from pathlib import Path
import shutil
import subprocess
import time
assert os.geteuid()==0 and os.environ.get('M4_TEST_CPU_MAX_KHZ'), 'mandatory root guard missing'
size=4*1024**3
root=Path('/swap');swap=root/'m4-host.swap'
assert subprocess.check_output(['findmnt','-n','-o','FSTYPE','/'],text=True).strip()=='btrfs'
assert shutil.disk_usage('/').free > size+4*1024**3
fstab=Path('/etc/fstab');old=fstab.read_text()
assert str(swap) not in old, 'swap entry already exists; inspect before changing it'
assert not root.exists(), 'preserve existing swap directory'
subprocess.run(['btrfs','subvolume','create',str(root)],check=True)
root.chmod(0o700)
subprocess.run(['btrfs','filesystem','mkswapfile','--size','4G',str(swap)],check=True)
swap.chmod(0o600)
subprocess.run(['swapon',str(swap)],check=True)
assert str(swap) in Path('/proc/swaps').read_text()
backup=fstab.with_name('fstab.before-m4-swap-'+time.strftime('%Y%m%dT%H%M%S',time.gmtime()))
shutil.copy2(fstab,backup)
new=fstab.with_name('fstab.m4-swap.new')
new.write_text(old.rstrip()+'\n\n# Host reserve for Steam microVM + pinned GPU allocations (2026-09-18)\n'+str(swap)+' none swap defaults,nofail 0 0\n')
shutil.copymode(fstab,new)
with new.open('rb') as f:os.fsync(f.fileno())
os.replace(new,fstab)
subprocess.run(['systemctl','daemon-reload'],check=True)
print(Path('/proc/swaps').read_text(),end='')
print('HOST_SWAP_READY',flush=True)
