#!/usr/bin/env python3
"""Extract checked Mach-O GPU text/metadata from a captured Metal archive.

Reuses the bounds-checked decoder from native-metal-compile.py. No compilation
or hardware access. Raw bytes and symbol maps are retained for partial ISA
analysis; extraction does not establish that every archived variant executed.
"""
import argparse
import json
import struct
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('archive', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
data = a.archive.read_bytes()
sections=[]
start=0
while (start:=data.find(b'\xcf\xfa\xed\xfe',start)) >= 0:
    if start+32 > len(data): break
    ncmd, sizeofcmds=struct.unpack_from('<II',data,start+16)
    cursor=start+32; end=cursor+sizeofcmds
    if end>len(data) or ncmd>sizeofcmds//8:
        start+=4; continue
    symbols=[]; local_sections=[]; section_index=0
    for _ in range(ncmd):
        if cursor+8>end: raise RuntimeError('Truncated Mach-O command')
        cmd,size=struct.unpack_from('<II',data,cursor)
        if size<8 or cursor+size>end: raise RuntimeError('Invalid Mach-O command')
        if cmd==2:
            if size<24: raise RuntimeError('Truncated symbol table command')
            symoff,nsyms,stroff,strsize=struct.unpack_from('<4I',data,cursor+8)
            if start+symoff+nsyms*16>len(data) or start+stroff+strsize>len(data):
                raise RuntimeError('Symbol table out of bounds')
            for j in range(nsyms):
                nameoff,typ,sect,desc,value=struct.unpack_from('<IBBHQ',data,start+symoff+j*16)
                if nameoff>=strsize: raise RuntimeError('Symbol name out of bounds')
                name_start=start+stroff+nameoff
                name_end=data.find(b'\0',name_start,start+stroff+strsize)
                if name_end<0: raise RuntimeError('Unterminated symbol name')
                if typ & 0x0e == 0x0e:
                    symbols.append({'name':data[name_start:name_end].decode(),
                                    'section':sect,'value':value})
        if cmd==0x19:
            if size<72: raise RuntimeError('Truncated segment')
            count=struct.unpack_from('<I',data,cursor+64)[0]
            if 72+80*count>size: raise RuntimeError('Truncated sections')
            for j in range(count):
                section_index+=1
                q=cursor+72+80*j
                name=data[q:q+16].split(b'\0')[0].decode()
                segment=data[q+16:q+32].split(b'\0')[0].decode()
                length,offset=struct.unpack_from('<QI',data,q+40)
                if segment.startswith('__GPU_'):
                    if start+offset+length>len(data): raise RuntimeError('Metadata out of bounds')
                    (a.output/f'{segment}-{name}-{start:x}.bin').write_bytes(data[start+offset:start+offset+length])
                if name=='__text':
                    if start+offset+length>len(data): raise RuntimeError('Text out of bounds')
                    path=f'text-{start:x}.bin'
                    (a.output/path).write_bytes(data[start+offset:start+offset+length])
                    item={'file':path,'macho_offset':start,'offset':start+offset,'bytes':length,
                          'section_index':section_index,'address':struct.unpack_from('<Q',data,q+32)[0]}
                    local_sections.append(item)
        cursor+=size
    for item in local_sections:
        item['symbols']=[{'name':s['name'],'offset':s['value']-item['address']}
                         for s in symbols if s['section']==item['section_index']]
        if any(not 0<=s['offset']<item['bytes'] for s in item['symbols']):
            raise RuntimeError('Text symbol out of bounds')
        sections.append(item)
    start+=4
(a.output/'sections.json').write_text(json.dumps(sections,indent=2)+'\n')
print(json.dumps({'archive': str(a.archive), 'sections': len(sections)}))
