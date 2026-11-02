#!/usr/bin/env python3
"""Offline regression checks. No helper execution or GPU operations."""
import base64
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools'))
from privacy_scan import scan_text,scan_tree

FIXTURES=['Qm9hcmQgU2VyLiBOby4gOiBQSDQwMC1DMDItMjU3OQ==', 'UHJvamVjdCA6IEg0MDAtMDEwMA==', 'Uk9NIEhhc2ggOiAzMkQxQjczQThDQ0NCRjUzNzhDQTYwN0RGRjA5MkM0OA==', 'R1BVLTljZGY5MDE04oCm', 'Ym9vdCBpZCA2NDQxZWQ1NOKApg==', 'Ym9vdCBpZCA4NzVjZjUwMeKApg==', 'cGFzc3dvcmRsZXNzIHN1ZG8=', 'dXNlciBsYWI=', 'c3NoIGBsYWJg', 'L1VzZXJzL3JvYmVydC9Eb2N1bWVudHMvcHJpdmF0ZQ==', 'MTkyLjE2OC44LjIxOA==', 'cmVzZWFyY2gvYXJjaGl2ZS9sYWI5dGIvc291cmNlLmM=']

class ReleaseGuards(unittest.TestCase):
 def test_identity_regressions(self):
  for encoded in FIXTURES[:-1]:
   text=base64.b64decode(encoded).decode()
   with self.subTest(fixture=encoded):self.assertTrue(scan_text(text))
 def test_host_directory_regression(self):
  with tempfile.TemporaryDirectory() as d:
   f=Path(d)/base64.b64decode(FIXTURES[-1]).decode();f.parent.mkdir(parents=True);f.write_text('example')
   self.assertTrue(scan_tree(Path(d)))
 def test_redacted_fields_and_registers_allowed(self):
  self.assertFalse(scan_text('Board Ser. No. : ID-REDACTED\nROM Hash : ID-REDACTED\nLnkCap 00453d01, BAR0+0x8c040'))
 def test_validator_fails_even_after_hash_renewal(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d)/'copy';shutil.copytree(ROOT,root,ignore=shutil.ignore_patterns('.git','__pycache__','.DS_Store'))
   (root/'injected.txt').write_text(base64.b64decode(FIXTURES[0]).decode())
   subprocess.run([sys.executable,'-B',str(root/'tools/refresh_manifest.py')],check=True,capture_output=True)
   r=subprocess.run([sys.executable,'-B',str(root/'tools/check_bundle.py')],capture_output=True,text=True)
   self.assertNotEqual(r.returncode,0)
   self.assertIn('Privacy pattern:',r.stdout)
   self.assertNotIn('Checksum mismatch',r.stdout)
 def test_builder_repeatability(self):
  with tempfile.TemporaryDirectory() as d:
   for sub in ['a','b']:
    subprocess.run([sys.executable,'-B',str(ROOT/'tools/build_release.py'),'--output-dir',str(Path(d)/sub)],check=True,capture_output=True)
   self.assertEqual((Path(d)/'a/cmp100-100-unlock-20261102.zip').read_bytes(),(Path(d)/'b/cmp100-100-unlock-20261102.zip').read_bytes())

if __name__=='__main__':unittest.main()
