"""Offline selection regressions; no hardware or target access."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('paired',Path(__file__).with_name('cts-paired.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class Selection(unittest.TestCase):
    def test_resume_keeps_original_indices(self):
        names=['dEQP-VK.api.buffer_marker.a','dEQP-VK.api.buffer_marker.b',
               'dEQP-VK.api.buffer_memory_requirements.a']
        raw,meta=m.selection(names,[1,2],{'catalog_sha256':'frozen'})
        self.assertEqual(raw.decode().splitlines(),names[1:])
        self.assertEqual([r['catalog_index'] for r in meta['cases']],[1,2])
        self.assertEqual(meta['provenance']['catalog_sha256'],'frozen')
        self.assertEqual(m.family(names[0]),m.family(names[1]))
        self.assertNotEqual(m.family(names[1]),m.family(names[2]))

if __name__=='__main__':unittest.main()
