from pathlib import Path
import json,os,subprocess,sys
out=Path(sys.argv[1]);guard=json.loads((out/'start.json').read_text())
assert guard['watchdog']['armed'] and guard['watchdog']['timeout_seconds']==60
subprocess.run(['python3','/root/idle-display-session.py',str(out)],check=True,timeout=32)
base=Path('/root/roadmap-fault-probe')
env=dict(os.environ,VK_DRIVER_FILES='/opt/asahi-m3m4-cts-20260927/asahi.json',
         VK_ICD_FILENAMES='/opt/asahi-m3m4-cts-20260927/asahi.json',
         LD_LIBRARY_PATH='/opt/asahi-m3m4-cts-20260927/lib',
         ASAHI_M3_EXPERIMENTAL='1',ASAHI_M4_EXPERIMENTAL='1',
         ASAHI_MESA_DEBUG='nocompress',HK_DEBUG_LOG='0',AGX_MESA_DEBUG='',
         VK_LAYER_PATH='/var/empty',VK_IMPLICIT_LAYER_PATH='/var/empty',
         M4_ORACLE_EXPECT_DEVICE_LOST='1')
(out/'fault-probe-environment.json').write_text(json.dumps(env,indent=2)+'\n')
cmd=[str(base/'vk-buffer-oracle'),str(base/'probe.spv'),str(base/'input.bin'),str(base/'expected.bin'),'1',str(out/'actual.bin')]
r=subprocess.run(cmd,env=env,timeout=15)
(out/'fault-probe-exit.json').write_text(json.dumps(dict(returncode=r.returncode))+'\n')
os.sync()
raise SystemExit(r.returncode)
