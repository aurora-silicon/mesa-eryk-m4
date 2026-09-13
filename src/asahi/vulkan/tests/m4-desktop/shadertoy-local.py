#!/usr/bin/env python3
"""Package the retained XdsGDB source/header/noise texture into an offline page.

Usage: shadertoy-local.py SOURCE_DIRECTORY OUTPUT.html
The source directory contains Buoy.glsl, header.glsl, channel0.png, manifest.json.
No network requests; the original licensed shader is embedded unchanged.
"""
import argparse
import base64
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
p.add_argument('output', type=Path)
a = p.parse_args()
payload = dict(source=(a.source/'Buoy.glsl').read_text(),
               header=(a.source/'header.glsl').read_text(),
               texture='data:image/png;base64,'+base64.b64encode((a.source/'channel0.png').read_bytes()).decode(),
               manifest=json.loads((a.source/'manifest.json').read_text()))
template = Path(__file__).with_name('shadertoy-local.template.html').read_text()
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(template.replace('/*PAYLOAD*/null', json.dumps(payload).replace('</', '<\\/')))
print(a.output)
