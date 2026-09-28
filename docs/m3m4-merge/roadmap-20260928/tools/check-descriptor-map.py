import hashlib,json,os,subprocess,sys
from pathlib import Path
output=Path(sys.argv[1]);prefix=sys.argv[2];expected='-1'
files=[Path(prefix)/'lib/libvulkan_asahi.so',Path('/root/vulkan-descriptor-map-failure'),Path('/root/asahi-lazy-map-failure.so')]
(output/'descriptor-map-identity.json').write_text(json.dumps({str(p):hashlib.file_digest(p.open('rb'),'sha256').hexdigest() for p in files},indent=2))
env=dict(os.environ,VK_DRIVER_FILES=prefix+'/asahi.json',VK_ICD_FILENAMES=prefix+'/asahi.json',LD_LIBRARY_PATH=prefix+'/lib',LD_PRELOAD='/root/asahi-lazy-map-failure.so',ASAHI_TEST_MAP_SIZE='16384',ASAHI_TEST_MAP_MATCH='1',ASAHI_TEST_DESCRIPTOR_POOL='1',ASAHI_TEST_EXPECTED_RESULT=expected,MESA_SHADER_CACHE_DISABLE='true',ASAHI_M3_EXPERIMENTAL='1',ASAHI_M4_EXPERIMENTAL='1',HK_DEBUG_LOG='0',AGX_MESA_DEBUG='',ASAHI_MESA_DEBUG='nocompress')
(output/'descriptor-map-environment.json').write_text(json.dumps({k:v for k,v in env.items() if k.startswith(('VK_','LD_','ASAHI','MESA_','HK_','AGX_'))},indent=2))
with (output/'descriptor-map.log').open('w') as log:
 rc=subprocess.run(['/root/vulkan-descriptor-map-failure'],env=env,stdout=log,stderr=subprocess.STDOUT).returncode
print((output/'descriptor-map.log').read_text(),flush=True)
(output/'descriptor-map-result.json').write_text(json.dumps(dict(returncode=rc,expected_vk_result=int(expected),matched=rc==0,scope='Userspace one-shot mapping injection; hardware readback on successful retry'))+'\n')
# A diagnostic mismatch is not device loss. Retain it without manufacturing a
# watchdog recovery; enclosing guard independently verifies terminal GPU health.
print('DESCRIPTOR_MAP_DIAGNOSTIC_COMPLETE matched='+str(rc==0),flush=True)
