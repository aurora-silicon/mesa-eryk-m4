#!/usr/bin/env python3
"""Aggregate host VM file descriptors without exposing user file paths."""
from pathlib import Path
import os,sys,collections,json
root=Path('/proc')/sys.argv[1]/'fd';counts=collections.Counter();unique={};sizes=collections.Counter()
for f in root.iterdir():
 try:
  name=os.readlink(f);s=f.stat()
  kind='dmabuf' if 'dmabuf' in name else 'memfd' if 'memfd' in name else 'drm' if name.startswith('/dev/dri/') else 'socket' if name.startswith('socket:') else 'pipe' if name.startswith('pipe:') else 'anon_inode' if name.startswith('anon_inode:') else 'file'
  counts[kind]+=1;unique[(s.st_dev,s.st_ino)]=(kind,s.st_size)
 except OSError:pass
for kind,size in unique.values():sizes[kind]+=size
print(json.dumps(dict(fd_counts=counts,unique_counts=collections.Counter(k for k,s in unique.values()),unique_bytes=sizes),indent=2))
