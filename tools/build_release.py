#!/usr/bin/env python3
"""Validate the current public tree, then build a deterministic ZIP outside it."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import zipfile

root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir',required=True,type=Path)
args=parser.parse_args()
out=args.output_dir.resolve()
if out==root or root in out.parents:parser.error('output directory must be outside the public source tree')
subprocess.run([sys.executable,'-B',str(root/'tools/check_bundle.py')],check=True)
entries=[]
for line in (root/'SHA256SUMS').read_text().splitlines():
 _,relative=line.split('  ',1);entries.append(relative)
entries.append('SHA256SUMS')
out.mkdir(parents=True,exist_ok=True)
archive=out/'cmp100-100-unlock-20261102.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
 for relative in sorted(entries):
  info=zipfile.ZipInfo('cmp100-100-unlock/'+relative,(2026,11,2,0,0,0))
  info.compress_type=zipfile.ZIP_DEFLATED
  mode=0o755 if relative=='unlock/cmp100-12gb.sh' else 0o644
  info.external_attr=(0o100000|mode)<<16
  z.writestr(info,(root/relative).read_bytes())
with zipfile.ZipFile(archive) as z:
 assert z.testzip() is None
 assert len(z.namelist())==len(entries)
 for relative in entries:
  assert z.read('cmp100-100-unlock/'+relative)==(root/relative).read_bytes()
sha=hashlib.sha256(archive.read_bytes()).hexdigest()
(out/'ZIP_SHA256SUMS').write_text(sha+'  '+archive.name+'\n')
(out/'LATEST_RELEASE.txt').write_text(archive.name+'\nSHA256='+sha+'\n')
print(f'ZIP verified: {len(entries)} files; SHA256={sha}')
print(archive)
