#!/usr/bin/env python3
"""Generate shared atomic64 progress, ordering, and full-capacity oracles.

Usage: shared-atomic64-ordering.py NEW_DIRECTORY
Run generated run.py OUTPUT ORACLE only under gpu-experiment.py. The oracle
must support M4_ORACLE_ATOMIC64=shared and M4_ORACLE_VULKAN_MEMORY_MODEL=1.
No GPU execution is performed by this generator.
"""
import json
from pathlib import Path
import struct
import subprocess
import sys

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=False)
common = '''#version 450
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_atomic_int64 : require
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_memory_scope_semantics : require
layout(set=0,binding=0,std430) buffer Out { uint output_words[]; };
'''
peer = common + '''layout(local_size_x=31,local_size_y=31) in;
shared uint64_t value[1922];
void main() {
    uint i=gl_LocalInvocationIndex;
    if (!subgroupBallotBitExtract(subgroupBallot(true),gl_SubgroupInvocationID^31u)) return;
    value[i+961]=uint64_t(0);
    subgroupBarrier();
    atomicExchange(value[i+961],uint64_t(1),gl_ScopeSubgroup,gl_StorageSemanticsShared,gl_SemanticsRelease);
    uint64_t old=atomicExchange(value[(i^31u)+961],uint64_t(2),gl_ScopeSubgroup,gl_StorageSemanticsShared,gl_SemanticsAcquire);
    output_words[gl_WorkGroupID.x*961+i]=old<=uint64_t(2)?1u:0u;
}
'''
ordering = common.replace('#version 450', '#version 450\n#pragma use_vulkan_memory_model') + '''
layout(local_size_x=31,local_size_y=31) in;
shared uint64_t payload[961], flag[961];
void main() {
    uint i=gl_LocalInvocationIndex, peer=i^31u;
    if (!subgroupBallotBitExtract(subgroupBallot(true),gl_SubgroupInvocationID^31u)) return;
    payload[i]=uint64_t(0); flag[i]=uint64_t(0);
    subgroupBarrier();
    uint64_t before=payload[peer];
    atomicExchange(flag[i],uint64_t(1),gl_ScopeSubgroup,gl_StorageSemanticsShared,gl_SemanticsRelease);
    uint64_t observed=atomicExchange(flag[peer],uint64_t(2),gl_ScopeSubgroup,gl_StorageSemanticsShared,gl_SemanticsAcquire);
    if(observed!=uint64_t(0)) payload[i]=uint64_t(i+1);
    output_words[gl_WorkGroupID.x*961+i]=before==uint64_t(0)?1u:0u;
}
'''
capacity = common + '''layout(local_size_x=THREADS) in;
shared uint64_t value[4096];
void main() {
    uint i=gl_LocalInvocationIndex, g=gl_WorkGroupID.x;
    for(uint k=i;k<4096;k+=THREADS) value[k]=uint64_t(g*4096+k);
    barrier();
    for(uint k=i;k<4096;k+=THREADS) {
        uint64_t old=atomicExchange(value[k],uint64_t(g*4096+k+123));
        output_words[g*4096+k]=uint(old);
    }
    barrier();
    for(uint k=i;k<4096;k+=THREADS)
        if(value[k]!=uint64_t(g*4096+k+123)) output_words[g*4096+k]=0xdeadbeefu;
}
'''
cases = []
for name, source, expected, vmm in [
    ('peer-progress', peer, ([1]*960+[0])*64, False),
    ('read-before-write', ordering, ([1]*960+[0])*64, True),
    ('full-shared-256', capacity.replace('THREADS','256'), list(range(64*4096)), False),
    ('full-shared-1024', capacity.replace('THREADS','1024'), list(range(64*4096)), False),
]:
    (root/(name+'.comp')).write_text(source)
    subprocess.run(['glslangValidator','-V','--target-env','vulkan1.2',str(root/(name+'.comp')),'-o',str(root/(name+'.spv'))], check=True)
    subprocess.run(['spirv-val','--target-env','vulkan1.2',str(root/(name+'.spv'))], check=True)
    (root/(name+'-input.bin')).write_bytes(bytes(4*len(expected)))
    (root/(name+'-expected.bin')).write_bytes(struct.pack('<'+str(len(expected))+'I',*expected))
    cases.append(dict(name=name,groups=64,vulkan_memory_model=vmm))
(root/'cases.json').write_text(json.dumps(cases,indent=2)+'\n')
(root/'run.py').write_text('''import json,os,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parent; out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
for c in json.loads((r/'cases.json').read_text()):
    name=c['name']; print('SHARED64_CASE:',name,flush=True)
    env=dict(os.environ,M4_ORACLE_ATOMIC64='shared')
    if c['vulkan_memory_model']: env['M4_ORACLE_VULKAN_MEMORY_MODEL']='1'
    else: env.pop('M4_ORACLE_VULKAN_MEMORY_MODEL',None)
    with (out/(name+'.log')).open('w') as log:
        p=subprocess.run([sys.argv[2],str(r/(name+'.spv')),str(r/(name+'-input.bin')),str(r/(name+'-expected.bin')),str(c['groups']),str(out/(name+'.bin'))],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=20)
    print(('PASS' if p.returncode==0 else 'FAIL')+': '+name,flush=True)
    if p.returncode: sys.exit(p.returncode)
print('PASS: four shared atomic64 ordering and capacity oracles',flush=True)
''')
print(root)
