#!/usr/bin/env python3
"""Guarded GEM ioctl boundary check. No GPU commands and no large allocations.

Reject zero and every sampled size whose 16KiB alignment would overflow u64;
map/touch/close a few valid small buffers to check the ordinary allocation path.
"""
from pathlib import Path
import errno,fcntl,json,mmap,os,struct,sys,subprocess,time
out=Path(sys.argv[1]);receipt=out/'start.json'
if not receipt.exists():receipt=out/'result.json'
state=json.loads(receipt.read_text())
assert state['watchdog']['armed'] and state['watchdog']['timeout_seconds']==60
# Values derived from drm/asahi_drm.h: GEM_CREATE id 5, MMAP_OFFSET id 6.
CREATE=(3<<30)|(24<<16)|(ord('d')<<8)|0x45
MMAP=(3<<30)|(16<<16)|(ord('d')<<8)|0x46
CLOSE=(1<<30)|(8<<16)|(ord('d')<<8)|0x09
memory_files=list(Path('/sys/kernel/debug').glob('asahi-*/memory'))
assert len(memory_files)==1,memory_files
memory_path=memory_files[0]
def memory_stats():
    lines=memory_path.read_text().splitlines()
    assert lines[0]=='version=1 unit=allocation_extent_bytes',lines
    return {parts[0]:{k:int(v) for k,v in (p.split('=') for p in parts[1:])}
            for parts in [line.split() for line in lines[1:]]}
before=None;after=None;rows=[];fd=None
try:
    subprocess.run(['python3','/root/idle-display-session.py',str(out)],check=True,timeout=32)
    before=memory_stats()
    fd=os.open('/dev/dri/renderD128',os.O_RDWR|os.O_CLOEXEC)
    for size in [0,*[((1<<64)-1)-delta for delta in [0,1,8191,16382]]]:
        data=bytearray(struct.pack('<Q4I',size,0,0,0,0))
        expected=errno.EINVAL if size==0 else errno.EOVERFLOW
        try:fcntl.ioctl(fd,CREATE,data,True)
        except OSError as error:
            assert error.errno==expected,(size,error.errno,expected)
            assert struct.unpack_from('<I',data,16)[0]==0
            rows.append(dict(size=size,errno=error.errno,handle=0))
        else:
            handle=struct.unpack_from('<I',data,16)[0]
            if handle:fcntl.ioctl(fd,CLOSE,struct.pack('<2I',handle,0))
            raise AssertionError(f'Invalid size {size} succeeded')
    for size in [1,16383,16384,16385,65535,65536]:
        data=bytearray(struct.pack('<Q4I',size,0,0,0,0));fcntl.ioctl(fd,CREATE,data,True)
        handle=struct.unpack_from('<I',data,16)[0];assert handle
        try:
            offset=bytearray(struct.pack('<2IQ',handle,0,0));fcntl.ioctl(fd,MMAP,offset,True)
            address=struct.unpack_from('<Q',offset,8)[0]
            length=(size+16383)&~16383
            with mmap.mmap(fd,length,flags=mmap.MAP_SHARED,prot=mmap.PROT_READ|mmap.PROT_WRITE,offset=address) as memory:
                assert memory[0]==0 and memory[length-1]==0
                memory[0]=37;memory[length-1]=91
                assert memory[0]==37 and memory[length-1]==91
            rows.append(dict(size=size,mapped_bytes=length,touched_both_ends=True,closed=True))
        finally:fcntl.ioctl(fd,CLOSE,struct.pack('<2I',handle,0))
finally:
    try:
        if fd is not None:os.close(fd)
        after=memory_stats()
        (out/'gem-memory-accounting.json').write_text(json.dumps(dict(before=before,after=after),indent=2)+'\n')
    finally:
        subprocess.run(['systemctl','start','display-manager'],check=True,timeout=15)
for kind in before:
    assert before[kind]['objects']==after[kind]['objects'],(kind,before,after)
    assert before[kind]['bytes']==after[kind]['bytes'],(kind,before,after)
(out/'gem-size-boundaries.json').write_text(json.dumps(rows,indent=2)+'\n')
print('GEM_SIZE_BOUNDARIES_PASS',flush=True)
