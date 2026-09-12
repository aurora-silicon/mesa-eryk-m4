"""Stage and atomically install a local Mesa build only after checksum verification."""
import argparse
import hashlib
from pathlib import Path
import re
import shlex
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path, nargs='?')
parser.add_argument('--restore-sha256', help='Restore an existing verified target backup without retransferring the library')
parser.add_argument('--label', required=True)
parser.add_argument('--component', choices=('vulkan', 'gallium'), default='vulkan')
parser.add_argument('--allow-live', action='store_true',
                    help='Atomic compatible userspace update; existing processes keep their old mapping. Never use for kernel/UAPI changes.')
args = parser.parse_args()
if bool(args.source) == bool(args.restore_sha256):
    parser.error('provide a source library or --restore-sha256, exclusively')
label = args.label
assert re.fullmatch(r'[a-z0-9-]+', label)
if args.restore_sha256:
    assert re.fullmatch(r'[0-9a-f]{64}', args.restore_sha256)
    digest = args.restore_sha256
    name = 'libvulkan_asahi.so' if args.component == 'vulkan' else 'libgallium-26.1.4.so'
    staged = '/opt/asahi-m4-port/lib/' + name + '.before-' + digest
else:
    source = args.source.resolve(strict=True)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    staged = '/root/mesa-' + args.component + '-' + label + '-' + digest + '.so'
    subprocess.run(['scp', '-o', 'ConnectTimeout=10', str(source), 'root@omarchy-m4:' + staged], check=True, timeout=90)
remote = '''import hashlib, os, pathlib, shutil, subprocess, sys
source, digest, component, allow_live = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4] == '1'
assert hashlib.sha256(source.read_bytes()).hexdigest() == digest, 'staged checksum mismatch'
clients = subprocess.run(['fuser', '/dev/dri/renderD128', '/dev/dri/card1'], capture_output=True)
assert clients.returncode in (0, 1), ('fuser error', clients.stdout, clients.stderr)
assert allow_live or clients.returncode == 1, ('GPU clients present; stop them or explicitly select compatible live userspace update', clients.stdout, clients.stderr)
name = 'libvulkan_asahi.so' if component == 'vulkan' else 'libgallium-26.1.4.so'
target = pathlib.Path('/opt/asahi-m4-port/lib')/name
assert target.is_file() and not target.is_symlink(), 'expected installed regular library'
previous = hashlib.sha256(target.read_bytes()).hexdigest()
backup = pathlib.Path(str(target)+'.before-'+previous)
if not backup.exists():
    shutil.copyfile(target, backup)
assert hashlib.sha256(backup.read_bytes()).hexdigest() == previous
temporary = target.with_suffix('.so.new')
shutil.copy2(source, temporary)
assert hashlib.sha256(temporary.read_bytes()).hexdigest() == digest, 'copy checksum mismatch'
with temporary.open('rb') as stream: os.fsync(stream.fileno())
os.replace(temporary, target)
os.sync()
assert hashlib.sha256(target.read_bytes()).hexdigest() == digest, 'installed checksum mismatch'
print('Installed', digest, target, 'previous', previous)
if allow_live:
    print('Existing processes retain their previous Mesa mapping; validate the new library in a fresh process.')
'''
subprocess.run(['ssh', '-o', 'ConnectTimeout=10', 'root@omarchy-m4', shlex.join(['python3', '-c', remote, staged, digest, args.component, str(int(args.allow_live))])], check=True, timeout=30)
