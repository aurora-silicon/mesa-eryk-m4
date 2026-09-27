#!/usr/bin/env python3
"""Validate versioned first-error records and old snapshot compatibility."""
import importlib.util
from pathlib import Path
import struct

def load(path):
    spec=importlib.util.spec_from_file_location('dump_parser',path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m

def make(magic,payload,name=b'host-first-error',va=0):
    return (magic + struct.pack('<IIQQQ',1,1,0,100,104+len(payload))+
            name.ljust(32,b'\0')+struct.pack('<4Q',va,len(payload),101,102)+payload)

for gpu, path in [('M3','tools/m3-gpu/fault_snapshot.py'),('M4','tools/g16-fault-dump.py')]:
    decode=load(Path(path)).decode
    magic=(gpu+'FWD001').encode()
    def payload(version=1,errno=-110,dma=1):
        return (struct.pack('<IiQ',version,errno,dma) if gpu=='M3' else
                struct.pack('<IiQQQ',version,errno,7,2,dma))
    result=decode(make(magic,payload()))
    assert result['first_error']['errno']==-110
    assert result['first_error']['dma_retirement_unproven'] is True
    if gpu=='M4': assert result['first_error']['inflight_commands']==7
    assert decode(make(magic,b'old-record',name=b'event-ring'))['first_error'] is None
    for bad in [payload(version=2),payload(errno=0),payload(dma=2),b'short']:
        try: decode(make(magic,bad))
        except ValueError: pass
        else: raise AssertionError('accepted malformed cause')
    try: decode(make(magic,payload(),va=1))
    except ValueError: pass
    else: raise AssertionError('accepted GPU pointer for host-only record')
    print('PASS',gpu,'old snapshots, first error, malformed ABI rejection')
