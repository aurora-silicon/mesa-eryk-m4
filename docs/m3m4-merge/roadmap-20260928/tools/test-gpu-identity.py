#!/usr/bin/env python3
"""Explicit private ICD and interposer selection must appear in provenance."""
import importlib.util,json,tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('identity',Path(__file__).with_name('gpu-identity.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);lib=p/'libvulkan_asahi.so';lib.write_bytes(b'identity-only');hook=p/'fault.so';hook.write_bytes(b'hook')
 icd=p/'asahi.json';icd.write_text(json.dumps({'ICD':{'library_path':str(lib)}}))
 env,files,unknown=m.selected_files(['env','VK_DRIVER_FILES='+str(icd),'LD_PRELOAD='+str(hook),'/root/probe'])
 assert files=={lib,hook,icd} and not unknown
 icd.write_text(json.dumps({'ICD':{'library_path':'./libvulkan_asahi.so'}}));assert lib in m.selected_files(['VK_DRIVER_FILES='+str(icd)])[1]
 icd.write_text(json.dumps({'ICD':{'library_path':'libvulkan_asahi.so'}}));assert m.selected_files(['VK_DRIVER_FILES='+str(icd)])[2]
 assert lib in m.selected_files(['VK_DRIVER_FILES='+str(icd),'LD_LIBRARY_PATH='+str(p)])[1]
 assert m.selected_files(['VK_DRIVER_FILES=missing.json'])[2]
 print('PASS absolute/relative/private-search ICD, preload hashes and explicit unresolved selection')
