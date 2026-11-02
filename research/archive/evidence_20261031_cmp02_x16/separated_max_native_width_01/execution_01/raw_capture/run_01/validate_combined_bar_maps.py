#!/usr/bin/env python3
"""Read-only v5 CPU gate plus planned BAR-map parity and final identity recheck."""
import argparse, importlib.util, json, sys
from pathlib import Path
HERE=Path(__file__).resolve().parent

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod
v5=load('validate_gate_05',HERE/'validate_gate_05.py')
scan=v5.scan
OFFSETS=(0x8c040,0x88150,0x88084,0x8b980,0x88088)

def validate_map_parity(planned_maps,store_range_map):
    reference={'mapping':store_range_map['mapping'],'vmflags':sorted(store_range_map['vmflags'])}
    for off,row in planned_maps.items():
        if row.get('metadata_only') is not True or row.get('mmio_bytes_read')!=0:
            raise ValueError(f'{off}: mapping check must remain metadata-only')
        actual={'mapping':row.get('mapping'),'vmflags':sorted(row.get('vmflags',[]))}
        if actual!=reference: raise ValueError(f'{off}: mapping differs from validated store-range BAR mapping')
    if set(planned_maps)!=set(hex(x) for x in OFFSETS):
        raise ValueError('planned BAR offset set mismatch')
    return True

def validate_identity(snapshot,current,expected_boot,pid,start_ticks,argv):
    if current.get('boot_id')!=expected_boot or snapshot.get('expected_boot_id')!=expected_boot:
        raise ValueError('final boot identity mismatch')
    if current.get('pid')!=pid or snapshot.get('pid')!=pid:
        raise ValueError('final PID identity mismatch')
    if current.get('pid_start_ticks')!=start_ticks or snapshot.get('pid_start_ticks')!=start_ticks:
        raise ValueError('final PID start ticks mismatch')
    if current.get('argv')!=argv or snapshot.get('argv')!=argv:
        raise ValueError('final argv identity mismatch')
    return True

def validate_live_maps(snapshot_path,pid,expected_boot):
    v5.EXPECTED_BOOT=expected_boot
    base=v5.validate_live(snapshot_path,pid,expected_boot)
    if not base.get('valid'):raise ValueError('v5 CPU gate failed: '+repr(base.get('errors')))
    snap=json.loads(Path(snapshot_path).read_text())
    if int(pid)!=int(snap['pid']):raise ValueError('requested PID differs from snapshot')
    start=v5.scan.proc_start_ticks(int(pid));argv=v5.proc_argv(int(pid))
    if start!=snap.get('pid_start_ticks') or argv!=snap.get('argv'):raise ValueError('snapshot PID/start-ticks/argv mismatch')
    if Path('/proc/sys/kernel/random/boot_id').read_text().strip()!=expected_boot:raise ValueError('boot changed before extended map check')
    smaps0=v5.parse_smaps(int(pid));maps0=v5.scan.parse_maps(int(pid));bar=int(base['mapped_bar_base'],16)
    planned={hex(off):v5.vmio_device_mapping(smaps0,bar+off,4) for off in OFFSETS}
    validate_map_parity(planned,base['store_range_map'])
    # Recheck map metadata and identity after the extension, before authorizing GO.
    smaps1=v5.parse_smaps(int(pid));maps1=v5.scan.parse_maps(int(pid))
    if [(r['raw'],sorted(r['vmflags'])) for r in smaps1] != [(r['raw'],sorted(r['vmflags'])) for r in smaps0]:
        raise ValueError('process smaps changed during planned-map validation')
    if [r['raw'] for r in maps1] != [r['raw'] for r in maps0]:raise ValueError('process maps changed during planned-map validation')
    current={'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
             'pid':int(pid),'pid_start_ticks':v5.scan.proc_start_ticks(int(pid)),
             'argv':v5.proc_argv(int(pid))}
    validate_identity(snap,current,expected_boot,int(pid),start,argv)
    return {'valid':True,'pid':int(pid),'pid_start_ticks':start,'boot_id':expected_boot,'argv':argv,
            'v5_cpu_gate':base,'planned_bar_mappings':planned,'planned_maps_match_store_map':True,
            'final_maps_rechecked':True,'metadata_only':True,'mmio_bytes_read':0}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('snapshot');ap.add_argument('pid',type=int);ap.add_argument('expected_boot');a=ap.parse_args()
    print(json.dumps(validate_live_maps(a.snapshot,a.pid,a.expected_boot),indent=2,sort_keys=True))
if __name__=='__main__':
    try:main()
    except Exception as e:print(json.dumps({'valid':False,'errors':[str(e)]},indent=2));sys.exit(2)
