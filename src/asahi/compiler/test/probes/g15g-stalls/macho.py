#!/usr/bin/env python3
"""Extract __TEXT,__text of every nested Mach-O inside metal-nt output."""
import struct, sys

def sections(buf, off=0):
    magic, cpu, sub, ftype, ncmds, sz, flags, _ = struct.unpack_from('<IiiIIIII', buf, off)
    assert magic == 0xfeedfacf, hex(magic)
    p = off + 32
    out = []
    for _ in range(ncmds):
        cmd, csz = struct.unpack_from('<II', buf, p)
        if cmd == 0x19:
            segname = buf[p+8:p+24].rstrip(b'\0').decode()
            nsects = struct.unpack_from('<I', buf, p+64)[0]
            q = p + 72
            for _ in range(nsects):
                sect = buf[q:q+16].rstrip(b'\0').decode()
                seg = buf[q+16:q+32].rstrip(b'\0').decode()
                addr, size, foff = struct.unpack_from('<QQI', buf, q+32)
                out.append((seg, sect, addr, size, off + foff))
                q += 80
        p += csz
    return out

def texts(buf, off=0, depth=0):
    """Yield (path, bytes) for every __text section, recursing into nested Mach-Os."""
    res = []
    for seg, sect, addr, size, foff in sections(buf, off):
        data = buf[foff:foff+size]
        if sect == '__text':
            res.append((f'{seg},{sect}', data))
        elif data[:4] == b'\xcf\xfa\xed\xfe':
            for path, d in texts(buf, foff, depth+1):
                res.append((f'{seg},{sect}/{path}', d))
    return res

if __name__ == '__main__':
    buf = open(sys.argv[1], 'rb').read()
    for path, d in texts(buf):
        print(path, len(d), d.hex())
