#!/usr/bin/env python3
"""Walk AGX3b code with Mesa's XML-driven Python disassembler (lengths + names)."""
import sys, os
ISA_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../../isa'))
sys.path.insert(0, ISA_DIR); sys.path.insert(0, ISA_DIR + '/test')
import isa as isamod
isamod.isa = isamod.load_isa(os.path.join(ISA_DIR, 'AGX3b.xml'))
import disasm as D
D.isa = isamod.isa
import contextlib, io

def walk(code, start=0):
    """Yield (offset, length, name, raw_bytes, asm)."""
    i = start
    while i < len(code):
        lst = code[i:i+16]
        raw = int.from_bytes(lst.ljust(16, b'\0'), 'little')
        length, instr, r = D.match_instr(raw)
        try:
            with contextlib.redirect_stdout(io.StringIO()):  # 'Unexpected set bits'
                l2, asm = D.disasm(raw)
        except Exception as e:
            asm = f'<err {e}>'
        name = instr.name if instr else None
        yield i, length, name, code[i:i+length], asm
        i += length

if __name__ == '__main__':
    code = bytes.fromhex(sys.argv[1]) if len(sys.argv) > 2 and sys.argv[2]=='hex' else open(sys.argv[1],'rb').read()
    for off, l, n, b, asm in walk(code, int(sys.argv[3],0) if len(sys.argv)>3 else 0):
        print(f'{off:4x}: {b.hex():<24} {n}: {asm}')
