#!/usr/bin/env python3
"""Parse the supplied memory log offline. Does not run CUDA or alter evidence."""
import hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parent
EXPECTED_SOURCE='b3e8f8855b719c9f21841f85f76e14250208ca20bf8873dc3cdbc9ab9f84f172'

def audit():
 source=ROOT/'cuda_vram_alias_test.c';log=ROOT/'vram_alias_20261102/full.log'
 assert hashlib.sha256(source.read_bytes()).hexdigest()==EXPECTED_SOURCE,'checker source differs from supplied note'
 text=log.read_text();parts=re.split(r'^=== FULL ([0-9a-f]{2}:[0-9a-f]{2}\.[0-7]) ===\n',text,flags=re.M)
 assert len(parts)==5,'expected exactly two labeled full runs'
 runs=[]
 for label,body,rc_label in [(parts[1],parts[2],'0'),(parts[3],parts[4],'1')]:
  total,free=map(int,re.search(r'^CUDA_TOTAL_MIB=(\d+) CUDA_FREE_MIB=(\d+)$',body,re.M).groups())
  alloc,count,segment=map(int,re.search(r'^ALLOC_MIB=(\d+) SEGMENTS=(\d+) SEGMENT_MIB=(\d+)$',body,re.M).groups())
  assert (alloc,count,segment)==(11264,44,256)
  assert alloc==((free-512)//segment)*segment and alloc==count*segment
  progress=[tuple(map(int,x)) for x in re.findall(r'^PASS_PROGRESS pass=(\d+) segment=(\d+)/(\d+) failures=(\d+)$',body,re.M)]
  assert progress==[(p,i,count,0) for p in (1,2) for i in range(1,count+1)],'incomplete/duplicate/failing progress'
  assert len(re.findall(r'^RESULT=PASS_FULL FAILURES=0 TESTED_MIB=11264$',body,re.M))==1
  assert re.search(r'^rc'+rc_label+r':0$',body,re.M)
  assert 'MISMATCH' not in body and 'failed:' not in body
  runs.append({'operator_label':label,'cuda_total_mib':total,'cuda_free_mib':free,'tested_mib':alloc,'passes':2,'segments':count,'segment_mib':segment,'progress_rows':len(progress),'reported_failures':0,'reported_rc':0})
 assert '---XID---\nEND ' in text,'unexpected Xid section; inspect manually'
 assert text.rstrip().endswith('REBIND_DONE')
 return {'audit':'PASS_OFFLINE_LOG_CONSISTENCY','source_sha256':EXPECTED_SOURCE,'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest(),'runs':runs,'limits':['BDFs are operator labels; checker calls CUDA logical device 0 and does not print its PCI ID.','No launch environment, checker ELF/build receipt or quick-run raw output is included.','Empty captured Xid section is not proof of full journal coverage.','11264 MiB allocated range tested; not every physical byte of the NVIDIA-reported 12288 MiB.','This is supplied runtime evidence, not a new test of the release initializer.']}

if __name__=='__main__':print(json.dumps(audit(),indent=2))
