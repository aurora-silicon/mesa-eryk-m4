#!/usr/bin/env python3
"""Compile a Metal compute/render pipeline or BTNF builtin for G16G on the Mac.

Runs Apple's offline compiler, not a GPU workload. Retains source, tool versions,
commands, archive, and checked Mach-O text sections in a new output directory.
"""
import argparse, base64, hashlib, json, shlex, struct, subprocess
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--function', default='cs')
p.add_argument('--stage', choices=['compute', 'render', 'builtin'], default='compute')
p.add_argument('--compiler', help='Absolute offline compiler path on the Mac; defaults to xcrun')
p.add_argument('--frontend', help='Absolute Metal frontend path on the Mac; defaults to xcrun metal')
p.add_argument('--remote-python', default='python3', help='Remote Python executable (may be an absolute standalone path)')
p.add_argument('--vertex-function', default='vs', help='Vertex function for render pipelines')
p.add_argument('--rasterization-disabled', action='store_true', help='Compile a render pipeline without rasterization or a fragment stage')
p.add_argument('--samples',type=int,default=1,choices=[1,2,4],help='Render pipeline raster sample count')
p.add_argument('--pixel-format',default='RGBA8Unorm',help='Native color attachment format for render pipelines')
p.add_argument('--host', default='m1@51.159.120.23')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
source = a.source.read_bytes()
source_name = 'source.btnf' if a.stage == 'builtin' else 'source.metal'
(a.output/source_name).write_bytes(source)
remote = r'''
import base64,json,pathlib,subprocess,sys,tempfile
request=json.load(sys.stdin)
root=pathlib.Path(tempfile.mkdtemp(prefix='m4-native-'))
(root/request['source_name']).write_bytes(base64.b64decode(request['source']))
if request['stage']=='compute':
    pipelines={'compute_pipelines':[{'compute_function':'alias:probe#'+request['function']}]}
elif request['stage']=='render':
    pipelines={'render_pipelines':[{
        'vertex_function':'alias:probe#'+request['vertex_function'],
        'raster_sample_count':request['samples'],
        'fragment_function':'alias:probe#'+request['function'],
        'color_attachments':[{'pixel_format':request['pixel_format']}]}]}
if request['stage']=='render' and request['rasterization_disabled']:
    pipelines['render_pipelines'][0]['rasterization_enabled']=False
    del pipelines['render_pipelines'][0]['fragment_function']
    del pipelines['render_pipelines'][0]['color_attachments']
if request['stage']!='builtin':
    spec={'libraries':{'paths':[{'label':'probe','path':'./source.metallib'}]},
          'pipelines':pipelines}
    (root/'pipeline.mtlp-json').write_text(json.dumps(spec,indent=2))
records=[]
def run(args):
    r=subprocess.run(args,cwd=root,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
    records.append({'argv':args,'returncode':r.returncode,'output':r.stdout})
    return r.returncode==0
compiler=([request['compiler']] if request['compiler'] else
          ['xcrun','metal-nt' if request['stage']=='builtin' else 'applegpu-nt'])
ok=run(['sw_vers']) and run(compiler+['--version'])
if request['stage']=='builtin':
    if ok:ok=run(compiler+['-arch','applegpu_g16g','-platform_version','macos','26.0','26.0','-N','source.btnf','-o','output.archive'])
else:
    frontend=([request['frontend']] if request['frontend'] else ['xcrun','metal'])
    if ok:ok=run(frontend+['source.metal','-o','source.metallib'])
    if ok:ok=run(compiler+['-arch','applegpu_g16g','-platform_version','macos','26.0','26.5','source.metallib','-N','pipeline.mtlp-json','-o','output.archive'])
files={f.name:base64.b64encode(f.read_bytes()).decode() for f in root.iterdir() if f.is_file()}
print(json.dumps({'ok':ok,'target':'applegpu_g16g','remote_directory':str(root),'commands':records,'files':files}))
'''
r = subprocess.run(['ssh','-o','ConnectTimeout=10',a.host,shlex.join([a.remote_python,'-c',remote])],
                   input=json.dumps({'source':base64.b64encode(source).decode(),'function':a.function,
                                     'stage':a.stage,'rasterization_disabled':a.rasterization_disabled,'vertex_function':a.vertex_function,'pixel_format':a.pixel_format,'samples':a.samples,
                                     'source_name':source_name,'compiler':a.compiler,'frontend':a.frontend}),
                   text=True,capture_output=True,timeout=180)
(a.output/'transport-stderr.log').write_text(r.stderr)
if r.returncode:
    raise SystemExit(f'Remote compiler transport failed ({r.returncode}); see {a.output}/transport-stderr.log')
result = json.loads(r.stdout)
files = result.pop('files')
result['host'] = a.host
result['stage'] = a.stage
result['sha256'] = {}
for name, data in files.items():
    if Path(name).name != name:
        raise RuntimeError('Invalid returned filename')
    data = base64.b64decode(data, validate=True)
    (a.output/name).write_bytes(data)
    result['sha256'][name] = hashlib.sha256(data).hexdigest()
(a.output/'manifest.json').write_text(json.dumps(result,indent=2)+'\n')
if not result['ok']:
    raise SystemExit('Compiler failed; see manifest.json')
data = (a.output/'output.archive').read_bytes()
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
print(json.dumps({'output':str(a.output),'target':result['target'],'sections':sections}))
