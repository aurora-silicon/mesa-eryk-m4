#!/usr/bin/env python3
"""Host desktop user: bounded CPU-only FEX probe, retaining primary VM output."""
import subprocess,sys
from pathlib import Path
binary=sys.argv[1];log=Path('/home/e/.cache/fex-cache-'+sys.argv[2]+'.log')
# The guest's initial command does not relay stdout through muvm -i.
code='import subprocess; from pathlib import Path; p=subprocess.run('+repr([binary,'/home/e/.cache/fex-thread-cache-probe'])+',stdout=subprocess.PIPE,stderr=subprocess.STDOUT); Path('+repr(str(log))+').write_bytes(p.stdout+b"\\nRETURN_CODE="+str(p.returncode).encode()+b"\\n")'
subprocess.run(['/usr/bin/muvm','-f','/usr/share/fex-emu/RootFS/default.erofs','--','python3','-c',code],timeout=22,check=True)
print(log.read_text(),flush=True)
