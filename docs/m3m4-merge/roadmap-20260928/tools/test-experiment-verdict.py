#!/usr/bin/env python3
"""A successful child/log marker cannot override terminal guard health failure."""
import importlib.util,json,tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('experiment',Path(__file__).with_name('gpu-experiment.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'remote').mkdir();(p/'remote/command.log').write_text('RENDER_OK\n')
 facts=dict(boot_id='same-boot',uptime_seconds=100)
 result=dict(boot_id='same-boot',returncode=0,watchdog=dict(timeout_seconds=60,armed=True),thermal=dict(samples=2,failure=None))
 def verdict():
  (p/'remote/result.json').write_text(json.dumps(result));return m.classify(p,facts,'RENDER_OK')
 assert verdict()=='pass'
 for error in ['GPU runtime latched unhealthy','GPU progress ABI mismatch','GPU runtime replaced during experiment','Stale GPU completion','']:
  result['progress_error']=error;assert verdict()=='gpu_progress_failure'
 (p/'remote/kernel-live.log').write_text('G16G: first runtime error errno=-5\n');assert verdict()=='gpu_fault'
 (p/'remote/kernel-live.log').unlink();del result['progress_error'];result['returncode']=1;assert verdict()=='process_failure'
 result['returncode']=0;result['thermal']['failure']='stale sensor';assert verdict()=='guard_validation_failed'
 print('PASS terminal health, malformed/stale/generation errors, primary fault, child and thermal verdicts')
