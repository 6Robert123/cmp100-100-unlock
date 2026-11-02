#!/usr/bin/env python3
"""Maintainer-only: accept current public bytes and renew metadata; not a validator."""
import hashlib
import json
from pathlib import Path

root=Path(__file__).resolve().parent.parent
provenance=root/'research/PROVENANCE.jsonl'
rows=[json.loads(line) for line in provenance.read_text().splitlines()]
for row in rows:
 row['public_sha256']=hashlib.sha256((root/row['public_path']).read_bytes()).hexdigest()
 row['transformed']=row['public_sha256']!=row['source_sha256']
provenance.write_text(''.join(json.dumps(row,ensure_ascii=False)+'\n' for row in rows))
files=sorted(p for p in root.rglob('*') if p.is_file() and '.git' not in p.relative_to(root).parts and p.name!='SHA256SUMS' and p.name!='.DS_Store' and '__pycache__' not in p.parts)
(root/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(root).as_posix()+'\n' for p in files))
print('Metadata renewed for current public bytes. Run check_bundle.py separately.')
