#!/usr/bin/env python3
"""Bounded identifier-pattern audit; not a proof that all private data is absent."""
import argparse
import re
from pathlib import Path

PREFIXES = [''.join(x) for x in [('6441','ed54'),('875c','f501'),('9cdf','9014'),('4270','dda5'),('accc','c883'),('4ee0','8b12'),('64ce','45a8'),('d52e','ce1e')]]
OLD_HOST = ''.join(('lab','9tb'))
RULES = {
 'source_host_label': re.compile(re.escape(chr(96)+'lab'+chr(96))),
 'gpu_identifier': re.compile(r'GPU-[0-9a-f]{8}(?:-[0-9a-f-]*)?',re.I),
 'uuid': re.compile(r'(?<![\w])[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}(?![\w])',re.I),
 'known_truncated_identifier': re.compile(r'(?<![0-9a-f])(?:'+'|'.join(PREFIXES)+r')(?![0-9a-f])',re.I),
 'private_network': re.compile(r'(?<![\w.])(?:192\.168\.\d+\.\d+|10\.\d+\.\d+\.\d+|172\.(?:1[6-9]|2\d|3[01])\.\d+\.\d+)(?![\w.])'),
 'private_user_path': re.compile(r'/(?:Users|home)/(?!LOCAL-USER\b|REMOTE-USER\b)[A-Za-z0-9_.-]+'),
 'board_serial_shape': re.compile(r'\b(?:032\d{10}|PH\d{3}-[A-Za-z0-9-]+)\b',re.I),
 'old_host_alias': re.compile(r'\b'+re.escape(OLD_HOST)+r'\b',re.I),
 'access_policy': re.compile(r'passwordless\s+sudo|\buser\s+lab\b|\bssh\s+`?lab(?:`|\s|$)',re.I),
 'private_key': re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
 'credential_shape': re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9]{20,}|sk-[A-Za-z0-9]{20,})\b'),
}
IDENTITY_FIELD = re.compile(r'^\s*(?:Board Ser\. No\.|Prod Ser\. No\.|Raw ECID(?: \(GHS\))?|ECID|PDI|Project|ROM Hash)\s*:\s*(.*)$',re.I)

def scan_text(text):
 findings=[]
 for n,line in enumerate(text.splitlines(),1):
  for category,rule in RULES.items():
   if rule.search(line):findings.append((n,category))
  m=IDENTITY_FIELD.match(line)
  if m and m.group(1).strip() and not re.fullmatch(r'[A-Z-]*REDACTED',m.group(1).strip()):
   findings.append((n,'unredacted_identity_field'))
 return findings

def scan_tree(root):
 findings=[]
 for p in sorted(root.rglob('*')):
  if '.git' in p.relative_to(root).parts:continue
  rel=str(p.relative_to(root))
  if OLD_HOST.lower() in rel.lower():findings.append((rel,0,'old_host_path'))
  if p.is_symlink():findings.append((rel,0,'symlink_not_reviewed'));continue
  if not p.is_file():continue
  if rel.startswith('docs/photos/') and rel.endswith('.jpg'):
   b=p.read_bytes()
   if b'Exif' in b[:4096] or b'GPS' in b[:4096] or b'\xff\xe1' in b[:4096]:findings.append((rel,0,'photo_metadata_present'))
   continue
  try:s=p.read_text()
  except UnicodeDecodeError:
   if rel!='unlock/historical/gp100_cfg1_race.x86_64.elf':findings.append((rel,0,'unreviewed_binary'))
   continue
  findings.extend((rel,n,c) for n,c in scan_text(s))
 return findings

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('root',nargs='?',type=Path,default=Path(__file__).resolve().parent.parent)
 args=parser.parse_args();findings=scan_tree(args.root)
 for path,line,category in findings:print(f'{path}:{line}: {category}')
 if findings:return 1
 print('PASS: supported identifier/credential patterns; binary exemption is the pinned project helper; docs/photos JPEGs must carry no EXIF/APP1 metadata.')
 print('Coverage is pattern-based; this is not a complete privacy/security guarantee.')
 return 0

if __name__=='__main__':raise SystemExit(main())
