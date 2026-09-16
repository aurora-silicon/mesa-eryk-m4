#!/usr/bin/env python3
"""Test a 32-bit-atomic critical section for complete 64-bit updates.

The return-value histogram checks every old counter value independently of
invocation order. This is a shader prototype, not Vulkan atomic64 support.
Run generated run.py only inside the mandatory watchdog/thermal guard.
"""
import json
from pathlib import Path
import struct
import subprocess
import sys

root = Path(sys.argv[1]); root.mkdir(parents=True, exist_ok=False)
alias = 'alias' in sys.argv[2:]
shared = 'shared' in sys.argv[2:]
native = 'native' in sys.argv[2:] or shared
if shared and alias: raise SystemExit('shared counters do not have sparse aliases')
cases = []
for count in [64, 256, 1024]:
    for label, start in [('carry', 0xffffff80), ('sign', 0x7fffffffffffff80),
                         ('wrap', 0xffffffffffffff80)]:
        name = ('alias-' if alias else '') + f'{label}-{count}'
        words = ((count + 32 + 4095) // 4096) * 4096 if alias else count + (64 if shared else 32)
        initial = [0xdeadc0de] * words
        initial[:4] = [start & 0xffffffff, start >> 32, 0, 0]
        initial[16:16+count] = [0] * count
        expected = list(initial)
        end = (start + count) & 0xffffffffffffffff
        expected[:2] = [end & 0xffffffff, end >> 32]
        if shared:
            expected[:2] = initial[:2]
            end = (start + 64) & 0xffffffffffffffff
            for group in range(count // 64):
                expected[count+16+group*2:count+18+group*2] = [end & 0xffffffff, end >> 32]
        expected[16:16+count] = [1] * count
        for suffix, values in [('input', initial), ('expected', expected)]:
            (root/(name+'-'+suffix+'.bin')).write_bytes(struct.pack('<'+str(len(values))+'I', *values))
        source = '''#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) coherent buffer Data {uint data[];} src;
uint64_t increment() {
 for(;;) {
  if(atomicCompSwap(src.data[2],0u,1u)==0u) {
   memoryBarrierBuffer();
   uint lo=atomicOr(src.data[0],0u),hi=atomicOr(src.data[1],0u);
   uint64_t old=uint64_t(lo)|(uint64_t(hi)<<32);
   uint64_t next=old+uint64_t(1);
   atomicExchange(src.data[0],uint(next));
   atomicExchange(src.data[1],uint(next>>32));
   memoryBarrierBuffer();
   atomicExchange(src.data[2],0u);
   return old;
  }
 }
}
void main(){
 uint64_t old=increment();
 uint64_t first=uint64_t(LOWu)|(uint64_t(HIGHu)<<32);
 uint64_t ordinal=old-first;
 if(ordinal<uint64_t(COUNTu)) atomicAdd(src.data[16u+uint(ordinal)],1u);
 else atomicOr(src.data[3],1u);
}
'''.replace('LOW', str(start & 0xffffffff)).replace('HIGH', str(start >> 32)).replace('COUNT', str(count))
        if alias:
            source = source.replace('layout(set=0,binding=0,std430) coherent buffer Data',
              'layout(set=0,binding=0,std430) coherent buffer Alias {uint data[];} other;\nlayout(set=0,binding=1,std430) coherent buffer Data')
            source = source.replace('increment() {', 'increment() {\n bool aliased=(gl_GlobalInvocationID.x&1u)==0u;')
            source = source.replace('uint lo=atomicOr(src.data[0],0u),hi=atomicOr(src.data[1],0u);',
              'uint lo=aliased?atomicOr(other.data[0],0u):atomicOr(src.data[0],0u);\n   uint hi=aliased?atomicOr(other.data[1],0u):atomicOr(src.data[1],0u);')
            source = source.replace('atomicExchange(src.data[0],uint(next));\n   atomicExchange(src.data[1],uint(next>>32));',
              'if(aliased){atomicExchange(other.data[0],uint(next));atomicExchange(other.data[1],uint(next>>32));}\n   else{atomicExchange(src.data[0],uint(next));atomicExchange(src.data[1],uint(next>>32));}')
        if native:
            # Use the prototype's independent histogram and guard bytes, but
            # replace its hand-written critical section with one Vulkan atomic.
            begin = source.index('uint64_t increment() {')
            end = source.index('void main(){', begin)
            declaration = 'layout(set=0,binding=0,std430) buffer Counter {uint64_t data[];} counter;\n'
            if alias:
                declaration += 'layout(set=0,binding=1,std430) buffer Counter2 {uint64_t data[];} counter2;\n'
            body = 'return atomicAdd(counter.data[0],uint64_t(1));'
            if alias:
                body = 'if((gl_GlobalInvocationID.x&1u)==0u) return atomicAdd(counter.data[0],uint64_t(1)); else return atomicAdd(counter2.data[0],uint64_t(1));'
            source = source[:begin] + declaration + 'uint64_t increment(){' + body + '}\n' + source[end:]
            source = source.replace('#version 450', '#version 450\n#extension GL_EXT_shader_atomic_int64 : require')
        if shared:
            begin = source.index('layout(set=0,binding=0,std430) buffer Counter')
            end = source.index('void main(){', begin)
            source = source[:begin] + 'shared uint64_t local_counter;\nuint64_t increment(){return atomicAdd(local_counter,uint64_t(1));}\n' + source[end:]
            source = source.replace(' uint64_t old=increment();',
              ' if(gl_LocalInvocationIndex==0u)local_counter=uint64_t(src.data[0])|(uint64_t(src.data[1])<<32);\n barrier();\n uint64_t old=increment();\n barrier();\n if(gl_LocalInvocationIndex==0u){src.data['+str(count+16)+'u+gl_WorkGroupID.x*2u]=uint(local_counter);src.data['+str(count+17)+'u+gl_WorkGroupID.x*2u]=uint(local_counter>>32);}')
            source = source.replace('ordinal<uint64_t('+str(count)+'u)', 'ordinal<uint64_t(64u)')
            source = source.replace('src.data[16u+uint(ordinal)]', 'src.data[16u+gl_WorkGroupID.x*64u+uint(ordinal)]')
        (root/(name+'.comp')).write_text(source)
        subprocess.run(['glslangValidator','-V',str(root/(name+'.comp')),'-o',str(root/(name+'.spv'))],check=True)
        subprocess.run(['spirv-val','--target-env','vulkan1.1',str(root/(name+'.spv'))],check=True)
        cases.append(dict(name=name, groups=count//64, env=dict(M4_ORACLE_BINDING_COUNT='2',M4_ORACLE_SPARSE_ALIAS='1',M4_ORACLE_ALIAS_FULL_VIEWS='1') if alias else {}))
if native:
    for case in cases: case['env']['M4_ORACLE_ATOMIC64'] = 'shared' if shared else '1'
(root/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
(root/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent;out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
failed=0
for c in json.loads((r/'cases.json').read_text()):
 name=c['name'];print('LOCK64_CASE:',name,flush=True)
 with (out/(name+'.log')).open('w') as log:
  p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/(name+'-input.bin')),str(r/(name+'-expected.bin')),str(c['groups']),str(out/(name+'.bin'))],env=dict(os.environ,**c['env']),stdout=log,stderr=subprocess.STDOUT,timeout=20)
 print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True);failed+=p.returncode!=0
 if p.returncode:break
print(('FAIL' if failed else 'PASS')+': nine atomic64 lock configurations',flush=True)
sys.exit(bool(failed))
''')
print(root, len(cases))
