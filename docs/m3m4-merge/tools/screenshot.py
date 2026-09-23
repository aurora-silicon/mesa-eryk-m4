#!/usr/bin/env python3
"""Wait for a fresh, complete Spectacle image; never reuse a prior capture."""
import hashlib,json,os,struct,subprocess,time,uuid
from pathlib import Path

def capture(env, output, log, *, user='eryk', timeout=6):
    output=Path(output)
    image=Path('/home')/user/'.cache'/('mesa-validation-'+uuid.uuid4().hex+'.png')
    assert not image.exists()
    started=time.time_ns(); deadline=time.monotonic()+timeout
    command=['runuser','-u',user,'--','spectacle','-b','-n','-f','-o',str(image)]
    subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,
                   check=True,timeout=max(.1,deadline-time.monotonic()))
    # An existing Spectacle process may accept a D-Bus request and return before
    # its asynchronous PNG writer finishes. A unique path also excludes stale
    # files from earlier games. IEND must be present before retaining bytes.
    while time.monotonic()<deadline:
        try:
            data=image.read_bytes()
            if data.startswith(b'\x89PNG\r\n\x1a\n') and data.endswith(b'\0\0\0\0IEND\xaeB`\x82'):
                assert data[12:16]==b'IHDR'
                width,height=struct.unpack('>II',data[16:24])
                assert width>0 and height>0
                output.write_bytes(data)
                output.with_suffix('.capture.json').write_text(json.dumps(dict(
                    source=str(image),started_ns=started,completed_ns=time.time_ns(),
                    source_mtime_ns=image.stat().st_mtime_ns,width=width,height=height,
                    bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),
                    method='Unique Spectacle output; waited for complete PNG IEND'),indent=2)+'\n')
                image.unlink()
                return
        except FileNotFoundError:pass
        time.sleep(.05)
    raise TimeoutError('Fresh complete Spectacle image was not written: '+str(image))
