#!/usr/bin/env python3
"""Offline-only public bundle verification. Never executes any GPU helper."""
import base64
import hashlib
import json
import re
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from privacy_scan import scan_tree

root = Path(__file__).resolve().parent.parent
errors = []
manifest = root / 'SHA256SUMS'
listed=set()

for line in manifest.read_text().splitlines():
    expected, relative = line.split('  ', 1)
    listed.add(relative)
    p = root / relative
    if p.is_symlink() or not p.resolve().is_relative_to(root.resolve()):
        errors.append('Unsafe manifest path: ' + relative)
        continue
    if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest() != expected:
        errors.append('Checksum mismatch: ' + relative)
actual={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and '.git' not in p.relative_to(root).parts and p.name!='SHA256SUMS'}
for extra in sorted(actual-listed):errors.append('Unmanifested file: '+extra)
for missing in sorted(listed-actual):errors.append('Manifest file absent: '+missing)
for relative,line,category in scan_tree(root):
    errors.append(f'Privacy pattern: {relative}:{line}: {category}')
script = (root / 'unlock/cmp100-12gb.sh').read_text()
m = re.search(r'^__HELPER_B64_BEGIN__\n(.*?)\n__HELPER_B64_END__$', script, re.M | re.S)
if not m:
    errors.append('Embedded helper markers missing')
else:
    binary = base64.b64decode(m.group(1), validate=False)
    expected = 'a1ca427cb84f3225a5d337ebeaed20cdf1673d6ea7a5ee68a0dacf2bc8d88de6'
    if hashlib.sha256(binary).hexdigest() != expected:
        errors.append('Embedded helper checksum mismatch')
    if binary != (root / 'unlock/historical/gp100_cfg1_race.x86_64.elf').read_bytes():
        errors.append('Embedded helper differs from historical ELF')
for line in (root / 'research/PROVENANCE.jsonl').read_text().splitlines():
    row = json.loads(line)
    p = root / row['public_path']
    if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest() != row['public_sha256']:
        errors.append('Provenance mismatch: ' + row['public_path'])
# Check local markdown links; missing historical references were converted to plain text.
for p in root.rglob('*.md'):
    for target in re.findall(r'\[[^\]\n]*\]\(([^)\n]+)\)', p.read_text()):
        if re.match(r'^(https?://|mailto:|#)', target):
            continue
        relative = target.split('#', 1)[0].strip('<>')
        if relative and not (p.parent / relative).exists():
            errors.append('Broken local link: ' + str(p.relative_to(root)) + ' -> ' + target)
if errors:
    print('\n'.join(errors))
    raise SystemExit(1)
print('PASS: public SHA256 manifest, provenance, embedded ELF identity and local Markdown links')
print('Privacy scan: PASS for supported patterns only; not a complete privacy guarantee.')
print('This validator does not execute the ELF or GPUs. CPU fixture checks and separately recorded hardware trials have distinct scopes.')
