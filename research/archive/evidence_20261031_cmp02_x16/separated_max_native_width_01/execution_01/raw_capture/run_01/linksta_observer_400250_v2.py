#!/usr/bin/env python3
"""Read-only PCIe LinkCap/LinkSta sampler. It never writes PCI config space or uses NVML."""
import argparse, datetime, json, os, pathlib, subprocess, sys, time

ENDPOINT='0000:02:00.0'
ROOTPORT='0000:00:02.0'
EXPECTED_VENDOR={ENDPOINT:'0x10de',ROOTPORT:'0x8086'}
VALID_WIDTHS={1,2,4,8,16,32}
VALID_SPEEDS={1,2,3,4,5,6}

def proc_info(pid):
    raw=pathlib.Path('/proc',str(pid),'stat').read_text(); rest=raw[raw.rfind(')')+2:].split()
    return {'pid':pid,'state':rest[0],'pgid':int(rest[2]),'start_ticks':int(rest[19])}
def process_start(pid):
    try: return proc_info(pid)
    except FileNotFoundError: return None
def vendor_id(bdf):
    return (pathlib.Path('/sys/bus/pci/devices',bdf,'vendor').read_text().strip())
def read_caps(bdf, timeout=2):
    argv=['setpci','-s',bdf,'CAP_EXP+0x0c.l','CAP_EXP+0x12.w']
    p=subprocess.Popen(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,start_new_session=True)
    try: out,err=p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try: st=proc_info(p.pid)
        except Exception: st={'pid':p.pid,'pgid':p.pid,'start_ticks':None}
        return {'status':'TIMEOUT_PROCESS_PRESERVED','pid':p.pid,'process':st,'argv':argv}
    lines=out.splitlines()
    if p.returncode!=0 or len(lines)!=2: return {'status':'READ_ERROR','rc':p.returncode,'stdout':out,'stderr':err,'argv':argv}
    try: cap=int(lines[0],16); sta=int(lines[1],16)
    except ValueError: return {'status':'UNKNOWN_NONHEX_READ','stdout':out,'stderr':err}
    if cap in (0xffff,0xffffffff) or sta==0xffff: return {'status':'UNKNOWN_ALL_ONES','link_cap_raw':f'0x{cap:08x}','link_sta_raw':f'0x{sta:04x}'}
    max_width=(cap>>4)&0x3f; speed=sta&0xf; width=(sta>>4)&0x3f
    training=bool(sta&(1<<11)); dll_capable=bool(cap&(1<<20)); dll_active=bool(sta&(1<<13)) if dll_capable else None
    if max_width not in VALID_WIDTHS: status='UNKNOWN_INVALID_MAX_WIDTH'
    elif training: status='LINK_TRAINING'
    elif speed==0 and width==0: status='LINK_DOWN_OR_UNKNOWN'
    elif speed not in VALID_SPEEDS or width not in VALID_WIDTHS: status='UNKNOWN_INVALID_LINKSTA'
    else: status='LINK_UP'
    return {'status':status,'link_cap_raw':f'0x{cap:08x}','link_sta_raw':f'0x{sta:04x}',
            'max_width':max_width,'speed_code':speed,'negotiated_width':width,'training':training,
            'dll_active_reporting_capable':dll_capable,'dll_active':dll_active}

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--duration',type=float,default=90.0); ap.add_argument('--hz',type=float,default=5.0); ap.add_argument('--out',required=True); ap.add_argument('--app-pid',type=int); ap.add_argument('--tail-after-app',type=float,default=3.0)
    a=ap.parse_args()
    if not (0.2<=a.duration<=90 and 5<=a.hz<=10 and 1<=a.tail_after_app<=10): ap.error('duration 0.2..90s, hz 5..10, tail 1..10s')
    out=pathlib.Path(a.out)
    if out.exists() or out.is_symlink(): ap.error('output path must be new')
    vendors={b:vendor_id(b) for b in (ENDPOINT,ROOTPORT)}
    if vendors!=EXPECTED_VENDOR: ap.error(f'PCI device identity mismatch: expected {EXPECTED_VENDOR}, got {vendors}')
    app=process_start(a.app_pid) if a.app_pid else None
    if a.app_pid and app is None: ap.error('supplied app PID is not running; observer must start during app window')
    out.parent.mkdir(parents=True,exist_ok=True)
    boot=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(); period=1/a.hz
    deadline=time.monotonic()+a.duration; records=0; unknown=0; terminal_at=None; tail_until=None
    with out.open('x') as f:
        while time.monotonic()<deadline:
            t0=time.monotonic(); stamp=datetime.datetime.now(datetime.timezone.utc).isoformat()
            ep=read_caps(ENDPOINT); rp=read_caps(ROOTPORT)
            row={'utc':stamp,'monotonic_ns':time.monotonic_ns(),'boot_id':boot,'vendors':vendors,
                 'endpoint_bdf':ENDPOINT,'endpoint':ep,'rootport_bdf':ROOTPORT,'rootport':rp,
                 'app_process_start':app,'observer_pid':os.getpid()}
            f.write(json.dumps(row,separators=(',',':'))+'\n'); f.flush(); os.fsync(f.fileno()); records+=1
            if ep['status'] not in ('LINK_UP','LINK_TRAINING','LINK_DOWN_OR_UNKNOWN') or rp['status'] not in ('LINK_UP','LINK_TRAINING','LINK_DOWN_OR_UNKNOWN'): unknown+=1
            if ep['status']=='TIMEOUT_PROCESS_PRESERVED' or rp['status']=='TIMEOUT_PROCESS_PRESERVED':
                print(json.dumps({'status':'READ_TIMEOUT_PROCESS_PRESERVED','records':records,'output':str(out),'last':row}),file=sys.stderr); return 2
            if a.app_pid and terminal_at is None:
                cur=process_start(a.app_pid)
                if cur is None or cur['start_ticks']!=app['start_ticks']:
                    terminal_at=time.monotonic(); tail_until=terminal_at+a.tail_after_app
            if tail_until is not None and time.monotonic()>=tail_until: break
            time.sleep(max(0,period-(time.monotonic()-t0)))
    status='READ_ONLY_COMPLETE' if unknown==0 else 'READ_ONLY_COMPLETE_WITH_UNKNOWN_ROWS'
    print(json.dumps({'status':status,'records':records,'unknown_rows':unknown,'output':str(out),'boot_id':boot,'vendors':vendors,'app_process_start':app,'app_terminal_observed':terminal_at is not None,'tail_after_app_seconds':a.tail_after_app if terminal_at is not None else None}))
    return 0 if unknown==0 else 2
if __name__=='__main__': raise SystemExit(main())
