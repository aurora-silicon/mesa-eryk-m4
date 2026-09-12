#!/usr/bin/env python3
"""Compare one diagnostic shader replacement on host lavapipe and guarded M4.

Retains the original trace and replacement, verifies the remote trace hash,
runs bounded software and hardware replays, and compares raw draw attachments.
This is adapted diagnostic evidence, never original-application qualification.
"""
import argparse, hashlib, json, os, re, shlex, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOST = 'root@omarchy-m4'
def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
def remote(argv):
    return subprocess.check_output(['ssh','-o','ConnectTimeout=10',HOST,shlex.join(argv)],timeout=30,text=True)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--name',required=True)
    p.add_argument('--trace',required=True,type=Path)
    p.add_argument('--remote-trace',required=True)
    p.add_argument('--shader',required=True,type=Path)
    p.add_argument('--handle',required=True,type=int)
    p.add_argument('--dump-config',required=True,type=Path)
    p.add_argument('--end-frame',type=int,default=2)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args()
    if not re.fullmatch('[a-z0-9][a-z0-9-]*',a.name) or a.handle<1 or a.end_frame<1:p.error('Invalid name, shader handle or frame')
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    shader_dir=out/'replacements';shader_dir.mkdir();shader=shader_dir/f'sh{a.handle}';shader.write_bytes(a.shader.read_bytes())
    config=out/'dump-config.json';config.write_bytes(a.dump_config.read_bytes())
    trace_hash=sha(a.trace)
    remote_hash=remote(['sha256sum','--',a.remote_trace]).split()[0]
    if remote_hash!=trace_hash:raise RuntimeError('Local/remote captures differ')
    exe=ROOT/'build/gfxreconstruct-host/tools/replay/gfxrecon-replay'
    icd=ROOT/'build/mesa-m4-reference/src/gallium/targets/lavapipe/lvp_devenv_icd.x86_64.json'
    lvp=icd.parent/'libvulkan_lvp.so'
    software=out/'software';software.mkdir()
    env={k:v for k,v in os.environ.items() if not k.startswith(('VK_','GFXRECON_','MESA_','AGX_','ASAHI_','HK_'))}
    env.update(VK_DRIVER_FILES=str(icd),LP_NUM_THREADS='4')
    command=[str(exe),'--swapchain','offscreen','--memory-translation','rebind','--omit-pipeline-cache-data',
             '--replace-shaders',str(shader_dir),'--dump-resources',str(config),'--dump-resources-dir',str(software),
             '--measurement-frame-range',f'1-{a.end_frame+1}','--quit-after-measurement-range',str(a.trace.resolve())]
    manifest=dict(trace=str(a.trace.resolve()),trace_sha256=trace_hash,remote_trace=a.remote_trace,
                  shader_sha256=sha(shader),config_sha256=sha(config),replay_sha256=sha(exe),lavapipe_sha256=sha(lvp),
                  software_command=command,adaptations=['SPIR-V replacement','memory rebinding','offscreen swapchain','omit pipeline cache'])
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    with (out/'software.log').open('w') as log:
        subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=25,check=True)
    staging=f'/root/shader-probes/{a.name}-{sha(shader)[:12]}'
    remote(['mkdir','-p',staging+'/replacements'])
    subprocess.run(['scp',str(shader),HOST+':'+staging+'/replacements/'],check=True,timeout=30)
    subprocess.run(['scp',str(config),HOST+':'+staging+'/dump-config.json'],check=True,timeout=30)
    command=['python3',str(ROOT/'tools/gpu-experiment.py'),'--name',a.name,
             '--hypothesis','Diagnostic shader replacement compared against the same saved software draws',
             '--expect','^PASS: Vulkan replay process completed','--seconds','40','--',
             'python3','/root/vulkan-trace.py','replay','--guard-output','{output}','--renderer','asahi','--seconds','25',
             '--trace',a.remote_trace,'--memory-translation','rebind','--omit-pipeline-cache',
             '--dump-config',staging+'/dump-config.json','--replace-shaders',staging+'/replacements','--end-frame',str(a.end_frame)]
    with (out/'hardware.log').open('w') as log:
        result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=360)
    text=(out/'hardware.log').read_text();match=re.search(r'^Evidence: (.+)$',text,re.M)
    if not match:raise RuntimeError('No guarded experiment evidence path; inspect hardware.log')
    evidence=Path(match[1]);manifest['hardware_evidence']=str(evidence);manifest['hardware_returncode']=result.returncode
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(text)
    result.check_returncode()
    subprocess.run(['python3',str(ROOT/'tools/vulkan-dump-compare.py'),str(software),str(evidence/'remote/vulkan-replay'),
                    '--output',str(out/'comparison.json')],check=True,timeout=30)
if __name__=='__main__':main()
