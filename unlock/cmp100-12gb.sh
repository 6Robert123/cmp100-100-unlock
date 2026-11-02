#!/usr/bin/env bash
# CMP100-100 UNLOCK managed by 6Robert123
set -Eeuo pipefail
exec python3 - "$0" "$@" <<'PY_ENGINE'
import argparse
import base64
import dataclasses
import fcntl
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HELPER_SHA256='a1ca427cb84f3225a5d337ebeaed20cdf1673d6ea7a5ee68a0dacf2bc8d88de6'
TARGET_CFG1='0x02669000'
SYS=pathlib.Path('/sys/bus/pci/devices')
NVIDIA=pathlib.Path('/sys/bus/pci/drivers/nvidia')
BDF=re.compile(r'[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]')
MANAGED='# CMP100-100 UNLOCK managed by 6Robert123'
INSTALL_SCRIPT=pathlib.Path('/usr/local/sbin/cmp100-12gb-unlock')
INSTALL_UNIT=pathlib.Path('/etc/systemd/system/cmp100-12gb-unlock.service')

@dataclasses.dataclass
class Device:
    bdf: str
    target: bool
    driver: object = None
    override: str = ''
    reset_methods: str = ''
    reset_method: str = ''
    boot_vga: bool = False

def null_override(value):
    value=value.strip()
    return '' if value in ('','(null)') else value

def parse_csv(text, expected):
    rows={}
    for line in text.splitlines():
        if not line.strip(): continue
        parts=[x.strip() for x in line.split(',')]
        if len(parts)!=2: raise RuntimeError('unexpected capacity CSV shape')
        bus=parts[0].lower()
        if bus.startswith('00000000:'):bus='0000:'+bus[9:]
        if not BDF.fullmatch(bus) or bus in rows:raise RuntimeError('invalid/duplicate capacity BDF')
        if not parts[1].isdigit():raise RuntimeError('invalid capacity value')
        rows[bus]=int(parts[1])
    if set(rows)!=set(expected):raise RuntimeError('capacity CSV missing or unexpected device')
    return rows

def helper_bytes(script_bytes):
    m=re.search(br'^__HELPER_B64_BEGIN__\n(.*?)\n__HELPER_B64_END__$',script_bytes,re.M|re.S)
    if not m:raise RuntimeError('helper markers absent')
    data=base64.b64decode(re.sub(br'\s+',b'',m.group(1)),validate=True)
    if hashlib.sha256(data).hexdigest()!=HELPER_SHA256:raise RuntimeError('embedded helper hash mismatch')
    return data

class Controller:
    """Actual orchestration, shared by Linux and in-memory CPU fixture backends."""
    def __init__(self,backend,mode='check',cold=False):
        self.b=backend;self.mode=mode;self.cold=cold
    def run(self,targets):
        self.b.preflight(targets,cold=self.cold,apply=self.mode!='check')
        if self.mode=='check':return None
        changed=[];watcher=None;result=None;failure=None
        self.b.acquire_lock()
        try:
            self.b.begin(targets)
            if self.cold:self.b.load_driver_once()
            self.b.capture_protected(targets)
            for d in targets:
                changed.append(d) # include partial prepare failures in cleanup
                self.b.prepare(d)
                watcher=self.b.arm(d)
                self.b.bind(d) # arm BEFORE any target driver probe
                self.b.probe_target(d,watcher) # target NVML while helper is live
                self.b.wait_helper(d,watcher)
                self.b.stop_helper(watcher);watcher=None
                self.b.target_query(d)
            result=self.b.final_query(targets)
            self.b.assert_success(targets,result)
        except BaseException as e:failure=e
        finally:
            try:
                if watcher is not None:self.b.stop_helper(watcher)
            except BaseException as e:failure=RuntimeError(str(failure or '')+'; helper cleanup: '+str(e))
            try:self.b.cleanup(changed)
            except BaseException as e:failure=RuntimeError(str(failure or '')+'; settings cleanup: '+str(e))
            if failure is None:
                try:self.b.verify_final_bindings(targets)
                except BaseException as e:failure=e
            self.b.release_lock()
        if failure is not None:
            self.b.report_failure(failure,changed)
            raise failure
        self.b.log('CMP100_12GB_READY count='+str(len(targets)))
        return result

class LinuxBackend:
    def __init__(self,script):
        self.script=pathlib.Path(script).resolve();self.lock=None;self.work=None;self.logdir=None
        self.helper=None;self.states={};self.allowed=set();self.protected={};self.initial={}
        self.current_watcher=None
    def log(self,s):
        print(s,flush=True)
        if self.logdir:
            with (self.logdir/'events.jsonl').open('a') as f:f.write(json.dumps({'monotonic':time.monotonic(),'message':s})+'\n')
    def cmd(self,args,timeout=15):
        try:
            r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=timeout)
        except subprocess.TimeoutExpired:raise RuntimeError('bounded command timed out: '+args[0])
        if r.returncode:raise RuntimeError('command failed rc='+str(r.returncode)+': '+args[0]+' '+r.stderr.strip()[:300])
        return r.stdout
    def write(self,path,value):
        path=pathlib.Path(path)
        valid=False
        for b in self.allowed:
            if path in [SYS/b/'reset',SYS/b/'reset_method',SYS/b/'driver_override']:
                valid=True
            if path in [NVIDIA/'bind',NVIDIA/'unbind'] and value.strip()==b:valid=True
        if not valid:raise RuntimeError('write outside selected CMP scope')
        self.log('WRITE '+str(path)+' value='+value.strip())
        self.cmd([sys.executable,'-c','import pathlib,sys; pathlib.Path(sys.argv[1]).write_text(sys.argv[2])',str(path),value],timeout=10)
    def driver(self,bdf):
        p=SYS/bdf/'driver'
        return p.resolve().name if p.is_symlink() else None
    def inventory(self):
        devices=[]
        for p in sorted(SYS.glob('*')):
            if not BDF.fullmatch(p.name):continue
            # A disappearing PCI function is a failed inventory, not silently omitted.
            vendor=(p/'vendor').read_text().strip().lower();dev=(p/'device').read_text().strip().lower()
            target=(vendor,dev)==('0x10de','0x15c2')
            override=(p/'driver_override').read_text() if (p/'driver_override').exists() else ''
            methods=(p/'reset_method').read_text().strip() if (p/'reset_method').exists() else ''
            devices.append(Device(p.name,target,self.driver(p.name),null_override(override),methods,boot_vga=(p/'boot_vga').exists() and (p/'boot_vga').read_text().strip()=='1'))
        return devices
    def topology(self,d):
        p=SYS/d.bdf
        if not (p/'reset').exists() or not (p/'reset_method').exists():raise RuntimeError(d.bdf+': reset controls unavailable')
        parent=p.resolve().parent
        if not BDF.fullmatch(parent.name) or (parent/'class').read_text().strip().lower()!='0x060400':
            raise RuntimeError(d.bdf+': PCI bridge parent not established')
        children={q.name for q in parent.iterdir() if BDF.fullmatch(q.name)}
        # Conservative for the bus-reset-only boards observed in this project.
        if children!={d.bdf}:raise RuntimeError(d.bdf+': shared reset bridge; no device reset will be attempted')
        methods=(p/'reset_method').read_text().split()
        selected=next((m for m in ('flr','af_flr','device','pm','bus') if m in methods),None)
        if not selected:raise RuntimeError(d.bdf+': no supported reset method')
        d.reset_method=selected;d.reset_methods=' '.join(methods)
    def fuser_pids(self,node):
        if not pathlib.Path(node).exists():return set()
        r=subprocess.run(['fuser',str(node)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=5)
        if r.returncode==1 and not r.stdout.strip() and not r.stderr.strip():return set()
        if r.returncode!=0:raise RuntimeError('cannot establish owners of '+str(node))
        if not r.stdout.strip() or not all(x.isdigit() for x in r.stdout.split()):raise RuntimeError('unparseable owner list for '+str(node))
        return {int(x) for x in r.stdout.split()}
    def clients(self,d):
        p=SYS/d.bdf
        for pattern in ('card[0-9]*','renderD[0-9]*'):
            for q in (p/'drm').glob(pattern):
                if self.fuser_pids('/dev/dri/'+q.name):raise RuntimeError(d.bdf+': active DRM client')
        info=pathlib.Path('/proc/driver/nvidia/gpus')/d.bdf/'information'
        if self.driver(d.bdf)=='nvidia':
            if not info.exists():raise RuntimeError(d.bdf+': cannot map NVIDIA device minor')
            text=info.read_text()
            m=re.search(r'^\s*(?:Device Minor|Minor Number)\s*:\s*(\d+)\s*$',text,re.M)
            if not m:raise RuntimeError(d.bdf+': missing NVIDIA minor mapping')
            if self.fuser_pids('/dev/nvidia'+m.group(1)):raise RuntimeError(d.bdf+': active GPU client')
        # Shared control/UVM-only ownership is ambiguous. Other cards may be present,
        # but a hot run with ambiguous clients must stop rather than detach a live GPU.
        for node in ('/dev/nvidiactl','/dev/nvidia-uvm','/dev/nvidia-uvm-tools'):
            if self.fuser_pids(node):raise RuntimeError('shared NVIDIA client cannot be safely attributed; stop the client or use an idle boot window')
    def identity(self,d):
        p=SYS/d.bdf
        if ((p/'vendor').read_text().strip().lower(),(p/'device').read_text().strip().lower())!=('0x10de','0x15c2'):
            raise RuntimeError(d.bdf+': target identity changed')
        if (p/'boot_vga').exists() and (p/'boot_vga').read_text().strip()=='1':raise RuntimeError(d.bdf+': boot VGA')
    def preflight(self,targets,cold,apply):
        if platform.system()!='Linux' or platform.machine()!='x86_64':raise RuntimeError('x86_64 Linux required')
        if os.geteuid()!=0:raise RuntimeError('root required for complete ownership/reset checks')
        for name in ['getconf','fuser','timeout','modprobe','setpci','nvidia-smi']:
            if not shutil.which(name):raise RuntimeError('missing command: '+name)
        libc=self.cmd(['getconf','GNU_LIBC_VERSION']).split()
        if len(libc)!=2 or libc[0]!='glibc' or tuple(map(int,libc[1].split('.')[:2]))<(2,38):raise RuntimeError('embedded helper requires glibc >=2.38')
        if apply and not cold and not pathlib.Path('/sys/module/nvidia').exists():raise RuntimeError('NVIDIA core absent; use --boot during an idle boot window')
        self.initial={d.bdf:d.driver for d in self.inventory()}
        self.allowed={d.bdf for d in targets}
        for d in targets:
            self.identity(d);self.topology(d)
            override=null_override((SYS/d.bdf/'driver_override').read_text())
            if override:raise RuntimeError(d.bdf+': custom driver override; refusing to replace it')
            if self.driver(d.bdf) not in (None,'nvidia'):raise RuntimeError(d.bdf+': target owned by another driver')
            self.clients(d)
        helper_bytes(self.script.read_bytes()) # hash check without extracting/executing
        self.log('READ_ONLY_PREFLIGHT_OK selected='+','.join(d.bdf for d in targets))
    def acquire_lock(self):
        self.lock=open('/run/cmp100-12gb.lock','a')
        fcntl.flock(self.lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    def release_lock(self):
        if self.lock:self.lock.close();self.lock=None
    def begin(self,targets):
        self.logdir=pathlib.Path(tempfile.mkdtemp(prefix='cmp100-12gb-',dir='/var/log'));self.logdir.chmod(0o700)
        self.log('LOGDIR='+str(self.logdir))
        self.work=self.executable_workdir()
        self.helper=self.work/'gp100_cfg1_race';self.helper.write_bytes(helper_bytes(self.script.read_bytes()));self.helper.chmod(0o700)
    def executable_workdir(self,parents=('/run','/var/tmp','/tmp')):
        # /run is often noexec. Test kernel execution in a private directory
        # before any driver load, detach or reset; never remount a filesystem.
        errors=[]
        for parent in parents:
            work=None
            try:
                work=pathlib.Path(tempfile.mkdtemp(prefix='cmp100-12gb-',dir=parent));work.chmod(0o700)
                probe=work/'exec-check';probe.write_text('#!/bin/sh\nexit 0\n');probe.chmod(0o700)
                self.cmd([str(probe)],timeout=3)
                probe.unlink()
                self.log('EXEC_WORKDIR_OK '+str(work))
                return work
            except (OSError,RuntimeError) as e:
                if work is not None:shutil.rmtree(work)
                errors.append(str(parent)+': '+str(e))
        raise RuntimeError('no executable private work directory before GPU changes: '+'; '.join(errors))
    def load_driver_once(self):
        if not pathlib.Path('/sys/module/nvidia').exists():self.cmd(['modprobe','nvidia'],timeout=30)
        if not pathlib.Path('/sys/module/nvidia').exists():raise RuntimeError('NVIDIA core did not load')
    def capture_protected(self,targets):
        selected={d.bdf for d in targets}
        current={d.bdf:d.driver for d in self.inventory()}
        for b,old in self.initial.items():
            if b not in selected and old is not None and current.get(b)!=old:raise RuntimeError('existing non-target binding changed during core load')
        self.protected={b:dr for b,dr in current.items() if b not in selected}
    def protected_unchanged(self):
        for b,expected in self.protected.items():
            if not (SYS/b).exists() or self.driver(b)!=expected:raise RuntimeError('non-target binding changed: '+b)
    def prepare(self,d):
        self.identity(d);self.topology(d);self.clients(d);self.protected_unchanged()
        p=SYS/d.bdf
        ov=(p/'driver_override').read_text();methods=(p/'reset_method').read_text();driver=self.driver(d.bdf)
        if null_override(ov) or driver not in (None,'nvidia'):raise RuntimeError(d.bdf+': ownership changed before prepare')
        self.states[d.bdf]={'override':ov,'methods':methods,'driver':driver,'reset_started':False,'override_written':False,'methods_written':False,'detached':False}
        st=self.states[d.bdf]
        # Hold prevents unsolicited matching/probing while the target is prepared.
        st['override_written']=True;self.write(p/'driver_override','cmp100-unlock-hold\n')
        st['methods_written']=True;self.write(p/'reset_method',d.reset_method+'\n')
        if driver=='nvidia':
            st['detached']=True;self.write(NVIDIA/'unbind',d.bdf+'\n')
        if self.driver(d.bdf) is not None:raise RuntimeError(d.bdf+': failed to detach')
        st['reset_started']=True;self.write(p/'reset','1\n')
        time.sleep(.15)
        self.cmd(['setpci','-s',d.bdf,'COMMAND=0002:0002'])
        command=self.cmd(['setpci','-s',d.bdf,'COMMAND']).strip()
        if not re.fullmatch('[0-9a-fA-F]{4}',command) or not int(command,16)&2:raise RuntimeError(d.bdf+': PCI memory decode readback failed')
    def arm(self,d):
        path=self.logdir/(d.bdf+'.helper.log');stream=path.open('w')
        proc=subprocess.Popen(['timeout','--signal=TERM','--kill-after=2s','20s',str(self.helper),d.bdf,TARGET_CFG1],stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
        watcher=(proc,stream,path);self.current_watcher=watcher
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            if proc.poll() is not None:raise RuntimeError(d.bdf+': helper exited before ARMED')
            if any(line.startswith('ARMED ') for line in path.read_text().splitlines()):return watcher
            time.sleep(.02)
        raise RuntimeError(d.bdf+': helper did not arm')
    def bind(self,d):
        self.identity(d);self.protected_unchanged()
        if self.driver(d.bdf) is not None:raise RuntimeError(d.bdf+': unexpected driver before bind')
        self.write(SYS/d.bdf/'driver_override','nvidia\n')
        self.write(NVIDIA/'bind',d.bdf+'\n')
        if self.driver(d.bdf)!='nvidia':raise RuntimeError(d.bdf+': explicit bind failed')
    def capacity(self,d):
        text=self.cmd(['nvidia-smi','--id='+d.bdf,'--query-gpu=pci.bus_id,memory.total','--format=csv,noheader,nounits'],timeout=8)
        rows=parse_csv(text,[d.bdf])
        self.log('CAPACITY '+d.bdf+' memory_mib='+str(rows[d.bdf]))
        if rows[d.bdf]!=12288:raise RuntimeError(d.bdf+': capacity '+str(rows[d.bdf])+'; expected 12288 MiB')
        return rows[d.bdf]
    def probe_target(self,d,watcher):
        status=watcher[0].poll()
        if status is not None:
            # Probe may finish initialization before the helper's observation
            # tail ends. A completed successful interception is also valid.
            if status!=0 or not re.search(r'^HIT_NS=',watcher[2].read_text(),re.M):
                raise RuntimeError(d.bdf+': helper exited without a valid interception before target NVML')
            self.log('HELPER_COMPLETED_DURING_BIND '+d.bdf)
        self.capacity(d)
    def wait_helper(self,d,w):
        proc,stream,path=w;rc=proc.wait(timeout=22);stream.flush()
        text=path.read_text()
        if rc!=0 or not re.search(r'^HIT_NS=',text,re.M):raise RuntimeError(d.bdf+': helper failed or no initialization interception')
        self.log('HELPER_PASS '+d.bdf)
    def stop_helper(self,w):
        if w is None:w=self.current_watcher
        if w is None:return
        proc,stream,_=w
        if proc.poll() is None:
            os.killpg(proc.pid,signal.SIGTERM)
            try:proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid,signal.SIGKILL);proc.wait(timeout=2)
        stream.close();self.current_watcher=None
    def target_query(self,d):return self.capacity(d)
    def final_query(self,targets):
        result={d.bdf:self.capacity(d) for d in targets}
        self.log('FINAL_CAPACITIES '+json.dumps(result,sort_keys=True))
        return result
    def assert_success(self,targets,result):
        if set(result)!={d.bdf for d in targets} or any(v!=12288 for v in result.values()):raise RuntimeError('final target capacity check failed')
        self.protected_unchanged()
    def cleanup(self,changed):
        errors=[]
        try:self.stop_helper(None)
        except Exception as e:errors.append('helper: '+str(e))
        for d in reversed(changed):
            st=self.states.get(d.bdf)
            if not st:continue
            p=SYS/d.bdf
            for key,path,value in [('methods_written',p/'reset_method',st['methods']),('override_written',p/'driver_override',('\n' if not null_override(st['override']) else st['override']))]:
                if st[key]:
                    try:self.write(path,value)
                    except Exception as e:errors.append(str(e))
            # Before any reset, an unsuccessful detach/unload-free preparation may
            # restore its original binding. After a reset, do not probe unarmed.
            if st['detached'] and not st['reset_started'] and st['driver']=='nvidia' and self.driver(d.bdf) is None:
                try:self.write(NVIDIA/'bind',d.bdf+'\n')
                except Exception as e:errors.append('pre-reset binding restore: '+str(e))
        if self.work:shutil.rmtree(self.work)
        if errors:raise RuntimeError('; '.join(errors))
    def verify_final_bindings(self,targets):
        self.protected_unchanged()
        for d in targets:
            if self.driver(d.bdf)!='nvidia' or null_override((SYS/d.bdf/'driver_override').read_text()):raise RuntimeError(d.bdf+': final binding/override check failed')
    def report_failure(self,error,changed):
        self.log('INITIALIZATION_FAILED '+str(error))
        for d in changed:
            self.log('PARTIAL_STATE '+d.bdf+' driver='+str(self.driver(d.bdf)))
        self.log('No global module unload, reset retry or automatic reboot was performed. Inspect logs before recovery.')

UNIT=(MANAGED+'\n'+'''[Unit]
Description=CMP100-100 12 GiB per-device initialization
Wants=systemd-udev-settle.service
After=systemd-udev-settle.service
Before=multi-user.target display-manager.service nvidia-persistenced.service

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/cmp100-12gb-unlock --boot
RemainAfterExit=yes
TimeoutStartSec=15min
KillMode=control-group

[Install]
WantedBy=multi-user.target
''').encode()

def install_plan(script):
    helper_bytes(script)
    if MANAGED.encode() not in script.split(b'\n',4):raise RuntimeError('script lacks managed marker')
    return [(INSTALL_SCRIPT,script,0o755),(INSTALL_UNIT,UNIT,0o644)]

def install_boot(script,remove=False):
    if platform.system()!='Linux' or platform.machine()!='x86_64' or os.geteuid()!=0:raise RuntimeError('boot installation requires root on x86_64 Linux')
    dependencies=['systemctl'] if remove else ['systemctl','getconf','fuser','timeout','modprobe','setpci','nvidia-smi']
    for dependency in dependencies:
        if not shutil.which(dependency):raise RuntimeError('missing installation prerequisite: '+dependency)
    if not remove:
        libc=subprocess.run(['getconf','GNU_LIBC_VERSION'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=5)
        fields=libc.stdout.split()
        if libc.returncode or len(fields)!=2 or fields[0]!='glibc' or tuple(map(int,fields[1].split('.')[:2]))<(2,38):
            raise RuntimeError('embedded helper requires glibc >=2.38; boot entry not installed')
    def ctl(*args,check=True):
        r=subprocess.run(['systemctl',*args],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=15)
        if check and r.returncode:raise RuntimeError('systemctl failed: '+' '.join(args))
        return r
    if ctl('show','--property=MainPID','--value',INSTALL_UNIT.name,check=False).stdout.strip() not in ('','0'):
        raise RuntimeError('initializer is currently running; installation changes refused')
    if not remove:
        if ctl('is-enabled','cmp100-12gb.service',check=False).returncode==0:
            raise RuntimeError('legacy cmp100-12gb.service is enabled; resolve duplicate startup manually first')
        plan=install_plan(pathlib.Path(script).read_bytes())
    else:plan=[(INSTALL_SCRIPT,b'',0),(INSTALL_UNIT,b'',0)]
    for path,_,_ in plan:
        if path.is_symlink():raise RuntimeError('managed path is a symlink: '+str(path))
        if path.exists() and MANAGED.encode() not in path.read_bytes().split(b'\n',4):raise RuntimeError('refusing to overwrite/remove unmanaged file: '+str(path))
    was_enabled=ctl('is-enabled',INSTALL_UNIT.name,check=False).returncode==0
    backups={p:(p.read_bytes(),p.stat().st_mode&0o777) if p.exists() else None for p,_,_ in plan}
    try:
        if remove:
            ctl('disable',INSTALL_UNIT.name)
            for path,_,_ in plan:
                if path.exists():path.unlink()
            ctl('daemon-reload')
        else:
            for path,data,mode in plan:
                path.parent.mkdir(parents=True,exist_ok=True)
                fd,tmp=tempfile.mkstemp(prefix='.'+path.name,dir=str(path.parent))
                try:
                    with os.fdopen(fd,'wb') as f:f.write(data)
                    os.chmod(tmp,mode);os.replace(tmp,path)
                finally:
                    if os.path.exists(tmp):os.unlink(tmp)
            ctl('daemon-reload');ctl('enable',INSTALL_UNIT.name)
    except Exception as original:
        rollback_errors=[]
        for path,old in backups.items():
            try:
                if old is None:
                    if path.exists():path.unlink()
                else:path.write_bytes(old[0]);path.chmod(old[1])
            except Exception as e:rollback_errors.append(str(e))
        for command in [('daemon-reload',),('enable' if was_enabled else 'disable',INSTALL_UNIT.name)]:
            try:ctl(*command)
            except Exception as e:rollback_errors.append(str(e))
        raise RuntimeError(str(original)+'; boot files/enable-state rollback '+('; '.join(rollback_errors) if rollback_errors else 'completed'))
    if remove:
        print('BOOT_ENTRY_REMOVED; no service stop, reset or reboot performed');return
    print('BOOT_ENTRY_INSTALLED_FOR_NEXT_BOOT; not started; no package/driver/blacklist change or reboot performed')

def parse(argv):
    parser=argparse.ArgumentParser(description='CMP100-100 UNLOCK by 6Robert123; hot --apply tested on two CMPs; mixed-adapter hosts and cold boot are not hardware tested.')
    group=parser.add_mutually_exclusive_group()
    group.add_argument('--check',action='store_true',help='read-only preflight (default); no nvidia-smi')
    group.add_argument('--apply',action='store_true',help='process selected CMPs with NVIDIA core already loaded')
    group.add_argument('--boot',action='store_true',help='boot window; load NVIDIA once if absent, then process CMPs')
    group.add_argument('--install-boot',action='store_true',help='install and enable next-boot entry; never start it now')
    group.add_argument('--uninstall-boot',action='store_true',help='remove this managed boot entry; never stop/reset GPUs')
    parser.add_argument('bdfs',nargs='*',help='optional domain-qualified CMP BDFs; default all 10de:15c2')
    return parser.parse_args(argv)

def main(argv=None,backend_factory=LinuxBackend,script=None):
    args=parse(sys.argv[2:] if argv is None else argv)
    script=script or sys.argv[1]
    if args.install_boot or args.uninstall_boot:
        if args.bdfs:raise RuntimeError('boot installation does not accept BDF subsets')
        install_boot(script,remove=args.uninstall_boot);return 0
    if platform.system()!='Linux' and backend_factory is LinuxBackend:raise RuntimeError('x86_64 Linux required; no device actions performed')
    backend=backend_factory(script)
    inventory=backend.inventory();found={d.bdf:d for d in inventory if d.target}
    for b in args.bdfs:
        if not BDF.fullmatch(b.lower()):raise RuntimeError('invalid BDF')
    names=[b.lower() for b in args.bdfs] if args.bdfs else sorted(found)
    if not names:
        if args.boot:print('SKIP: no CMP100-100 devices');return 0
        raise RuntimeError('no CMP100-100 device')
    if len(names)!=len(set(names)) or any(b not in found for b in names):raise RuntimeError('duplicate BDF or requested device is not a CMP100-100')
    Controller(backend,'apply' if args.apply or args.boot else 'check',args.boot).run([found[b] for b in names]);return 0

if __name__=='__main__':
    def interrupted(signum,frame):raise KeyboardInterrupt('signal '+str(signum))
    for sig in (signal.SIGINT,signal.SIGTERM,signal.SIGHUP):signal.signal(sig,interrupted)
    try:raise SystemExit(main())
    except (Exception,KeyboardInterrupt) as e:
        print('ERROR: '+str(e),file=sys.stderr);raise SystemExit(1)
PY_ENGINE
exit 0

__HELPER_B64_BEGIN__
f0VMRgIBAQAAAAAAAAAAAAMAPgABAAAAUBgAAAAAAABAAAAAAAAAAGA6AAAAAAAAAAAAAEAAOAAN
AEAAHwAeAAYAAAAEAAAAQAAAAAAAAABAAAAAAAAAAEAAAAAAAAAA2AIAAAAAAADYAgAAAAAAAAgA
AAAAAAAAAwAAAAQAAAAYAwAAAAAAABgDAAAAAAAAGAMAAAAAAAAcAAAAAAAAABwAAAAAAAAAAQAA
AAAAAAABAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADgLAAAAAAAAOAsAAAAAAAAAEAAA
AAAAAAEAAAAFAAAAABAAAAAAAAAAEAAAAAAAAAAQAAAAAAAABQoAAAAAAAAFCgAAAAAAAAAQAAAA
AAAAAQAAAAQAAAAAIAAAAAAAAAAgAAAAAAAAACAAAAAAAADAAwAAAAAAAMADAAAAAAAAABAAAAAA
AAABAAAABgAAADAtAAAAAAAAMD0AAAAAAAAwPQAAAAAAAOACAAAAAAAAIAMAAAAAAAAAEAAAAAAA
AAIAAAAGAAAAQC0AAAAAAABAPQAAAAAAAEA9AAAAAAAA8AEAAAAAAADwAQAAAAAAAAgAAAAAAAAA
BAAAAAQAAAA4AwAAAAAAADgDAAAAAAAAOAMAAAAAAAAwAAAAAAAAADAAAAAAAAAACAAAAAAAAAAE
AAAABAAAAGgDAAAAAAAAaAMAAAAAAABoAwAAAAAAAEQAAAAAAAAARAAAAAAAAAAEAAAAAAAAAFPl
dGQEAAAAOAMAAAAAAAA4AwAAAAAAADgDAAAAAAAAMAAAAAAAAAAwAAAAAAAAAAgAAAAAAAAAUOV0
ZAQAAABAIgAAAAAAAEAiAAAAAAAAQCIAAAAAAABEAAAAAAAAAEQAAAAAAAAABAAAAAAAAABR5XRk
BgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAFLldGQE
AAAAMC0AAAAAAAAwPQAAAAAAADA9AAAAAAAA0AIAAAAAAADQAgAAAAAAAAEAAAAAAAAAL2xpYjY0
L2xkLWxpbnV4LXg4Ni02NC5zby4yAAAAAAAEAAAAIAAAAAUAAABHTlUAAgAAwAQAAAADAAAAAAAA
AAKAAMAEAAAAAQAAAAAAAAAEAAAAFAAAAAMAAABHTlUAXU+1ajExN1wCzjtxJWt3ILN6ouwEAAAA
EAAAAAEAAABHTlUAAAAAAAMAAAACAAAAAAAAAAAAAAADAAAAFwAAAAEAAAAGAAAAAAGBAAABEAIX
AAAAGQAAAAAAAAAoHYwc0WXObTnyixwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACpAAAAEgAAAAAA
AAAAAAAAAAAAAAAAAADAAAAAEgAAAAAAAAAAAAAAAAAAAAAAAAA/AAAAEgAAAAAAAAAAAAAAAAAA
AAAAAABvAAAAEgAAAAAAAAAAAAAAAAAAAAAAAADWAAAAEgAAAAAAAAAAAAAAAAAAAAAAAABKAQAA
IAAAAAAAAAAAAAAAAAAAAAAAAAABAAAAEgAAAAAAAAAAAAAAAAAAAAAAAAAPAAAAEgAAAAAAAAAA
AAAAAAAAAAAAAADnAAAAEgAAAAAAAAAAAAAAAAAAAAAAAAC6AAAAEgAAAAAAAAAAAAAAAAAAAAAA
AAAtAAAAEgAAAAAAAAAAAAAAAAAAAAAAAABmAQAAIAAAAAAAAAAAAAAAAAAAAAAAAABOAAAAEgAA
AAAAAAAAAAAAAAAAAAAAAABHAAAAEgAAAAAAAAAAAAAAAAAAAAAAAAAgAAAAEgAAAAAAAAAAAAAA
AAAAAAAAAACIAAAAEgAAAAAAAAAAAAAAAAAAAAAAAABqAAAAEgAAAAAAAAAAAAAAAAAAAAAAAADP
AAAAEgAAAAAAAAAAAAAAAAAAAAAAAABcAAAAEgAAAAAAAAAAAAAAAAAAAAAAAAB1AQAAIAAAAAAA
AAAAAAAAAAAAAAAAAACgAAAAEgAAAAAAAAAAAAAAAAAAAAAAAADsAAAAEgAAAAAAAAAAAAAAAAAA
AAAAAABVAAAAEQAaACBAAAAAAAAACAAAAAAAAACRAAAAIgAAAAAAAAAAAAAAAAAAAAAAAACBAAAA
EQAaAEBAAAAAAAAACAAAAAAAAAAAY2xvY2tfZ2V0dGltZQBfX3N0YWNrX2Noa19mYWlsAF9fcHJp
bnRmX2NoawBzY2hlZF9zZXRhZmZpbml0eQBwdXRjaGFyAG11bm1hcABmZmx1c2gAc3Rkb3V0AF9f
ZnByaW50Zl9jaGsAb3BlbgBfX2xpYmNfc3RhcnRfbWFpbgBzdGRlcnIAbWxvY2thbGwAX19jeGFf
ZmluYWxpemUAc3RyZXJyb3IAX19pc29jMjNfc3RydG91bABjbG9zZQBfX3NucHJpbnRmX2NoawBm
d3JpdGUAX19lcnJub19sb2NhdGlvbgBtbWFwAGZzdGF0AGxpYmMuc28uNgBHTElCQ18yLjMzAEdM
SUJDXzIuNABHTElCQ18yLjE3AEdMSUJDXzIuMzQAR0xJQkNfMi4yLjUAR0xJQkNfMi4zLjQAR0xJ
QkNfMi4zOABfSVRNX2RlcmVnaXN0ZXJUTUNsb25lVGFibGUAX19nbW9uX3N0YXJ0X18AX0lUTV9y
ZWdpc3RlclRNQ2xvbmVUYWJsZQAAAAACAAMABAAFAAQAAQAGAAcABAAEAAMAAQAEAAQAAwAEAAQA
BAADAAEABAAIAAQABAAEAAAAAAABAAcA8gAAABAAAAAAAAAAs5GWBgAACAD8AAAAEAAAABRpaQ0A
AAcABwEAABAAAACXkZYGAAAGABEBAAAQAAAAtJGWBgAABQAcAQAAEAAAAHUaaQkAAAQAJwEAABAA
AAB0GWkJAAADADMBAAAQAAAAuJGWBgAAAgA/AQAAAAAAADA9AAAAAAAACAAAAAAAAAAwGQAAAAAA
ADg9AAAAAAAACAAAAAAAAADwGAAAAAAAAAhAAAAAAAAACAAAAAAAAAAIQAAAAAAAANg/AAAAAAAA
BgAAAAQAAAAAAAAAAAAAAOA/AAAAAAAABgAAAAYAAAAAAAAAAAAAAOg/AAAAAAAABgAAAAwAAAAA
AAAAAAAAAPA/AAAAAAAABgAAABQAAAAAAAAAAAAAAPg/AAAAAAAABgAAABgAAAAAAAAAAAAAACBA
AAAAAAAABQAAABcAAAAAAAAAAAAAAEBAAAAAAAAABQAAABkAAAAAAAAAAAAAAEg/AAAAAAAABwAA
AAEAAAAAAAAAAAAAAFA/AAAAAAAABwAAAAIAAAAAAAAAAAAAAFg/AAAAAAAABwAAAAMAAAAAAAAA
AAAAAGA/AAAAAAAABwAAAAUAAAAAAAAAAAAAAGg/AAAAAAAABwAAAAcAAAAAAAAAAAAAAHA/AAAA
AAAABwAAAAgAAAAAAAAAAAAAAHg/AAAAAAAABwAAAAkAAAAAAAAAAAAAAIA/AAAAAAAABwAAAAoA
AAAAAAAAAAAAAIg/AAAAAAAABwAAAAsAAAAAAAAAAAAAAJA/AAAAAAAABwAAAA0AAAAAAAAAAAAA
AJg/AAAAAAAABwAAAA4AAAAAAAAAAAAAAKA/AAAAAAAABwAAAA8AAAAAAAAAAAAAAKg/AAAAAAAA
BwAAABAAAAAAAAAAAAAAALA/AAAAAAAABwAAABEAAAAAAAAAAAAAALg/AAAAAAAABwAAABIAAAAA
AAAAAAAAAMA/AAAAAAAABwAAABMAAAAAAAAAAAAAAMg/AAAAAAAABwAAABUAAAAAAAAAAAAAANA/
AAAAAAAABwAAABYAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAPMPHvpIg+wI
SIsF2S8AAEiFwHQC/9BIg8QIwwAAAAAA/zUSLwAA/yUULwAADx9AAPMPHvpoAAAAAOni////ZpDz
Dx76aAEAAADp0v///2aQ8w8e+mgCAAAA6cL///9mkPMPHvpoAwAAAOmy////ZpDzDx76aAQAAADp
ov///2aQ8w8e+mgFAAAA6ZL///9mkPMPHvpoBgAAAOmC////ZpDzDx76aAcAAADpcv///2aQ8w8e
+mgIAAAA6WL///9mkPMPHvpoCQAAAOlS////ZpDzDx76aAoAAADpQv///2aQ8w8e+mgLAAAA6TL/
//9mkPMPHvpoDAAAAOki////ZpDzDx76aA0AAADpEv///2aQ8w8e+mgOAAAA6QL///9mkPMPHvpo
DwAAAOny/v//ZpDzDx76aBAAAADp4v7//2aQ8w8e+mgRAAAA6dL+//9mkPMPHvr/JZ4uAABmDx9E
AADzDx76/yXeLQAAZg8fRAAA8w8e+v8l1i0AAGYPH0QAAPMPHvr/Jc4tAABmDx9EAADzDx76/yXG
LQAAZg8fRAAA8w8e+v8lvi0AAGYPH0QAAPMPHvr/JbYtAABmDx9EAADzDx76/yWuLQAAZg8fRAAA
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
8w8e+oA9TScAAAB1K1VIgz3yJgAAAEiJ5XQMSIs99iYAAOg5+P//6GT////GBSUnAAABXcMPHwDD
Dx+AAAAAAPMPHvrpd////w8fgAAAAABTQYnySIn7QYnTvgQCkAAxwIPhAQ8fQABBD6PCcjGJ8sHq
Ao0UlQAAAABIAdqLOoH/AJBVAkEPlcFBhMl1EUQ533QMRIkaTYXAdARBgwABg8ABgcYAQAAAg/gQ
dbvwSIMMJABbw2ZmLg8fhAAAAAAASIPsKL8EAAAAZEiLBCUoAAAASIlEJBgxwEiJ5ujP9///SGkE
JADKmjtIA0QkCEiLVCQYZEgrFCUoAAAAdQVIg8Qow+i49///8w8e+kiD7AhIg8QIwwAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAAIAAAAAAHVzYWdlOiAl
cyAwMDAwOjAxOjAwLjAgVEFSR0VUX0NGRzFfSEVYCgAvc3lzL2J1cy9wY2kvZGV2aWNlcy8lcy9y
ZXNvdXJjZTAAAAAAAAAAQVJNRUQgdHJpZ2dlcj0weCUwOHggdGFyZ2V0PTB4JTA4eCBPUFRfRkJJ
Tz0weCUwOHggYWN0aXZlPQAAAAAAAEhJVF9OUz0lbHUgREVMVEFfVVM9JWx1IFJFQURCQUNLPTB4
JTA4eCBDU1RBVFVTPTB4JTA4eCBMTVI9MHglMDh4IERFVklOSVQ9MHglMDh4IFJFV1JJVEVTPSV1
IE9QVF9GQklPPTB4JTA4eAoAAABPQlNFUlZFX1VTPSVsdSBDRkcxPTB4JTA4eCBDU1RBVFVTPTB4
JTA4eCBMTVI9MHglMDh4IERFVklOSVQ9MHglMDh4CgAAAABGSU5BTCBDRkcxPTB4JTA4eCBDU1RB
VFVTPTB4JTA4eCBMTVI9MHglMDh4IERFVklOSVQ9MHglMDh4CgAAAAAARklOQUxfRkJQQSUwMnUg
Q0ZHMT0weCUwOHggQ1NUQVRVUz0weCUwOHgKAAAAAAAAVElNRU9VVCBDRkcxPTB4JTA4eCBMTVI9
MHglMDh4IERFVklOSVQ9MHglMDh4IE9QVF9GQklPPTB4JTA4eAoAaW52YWxpZCB0YXJnZXQ6ICVz
CgBvcGVuOiAlcwoAQkFSMCB1bmF2YWlsYWJsZQoAbW1hcDogJXMKACV1JXMALAABGwM7RAAAAAcA
AADg7f//eAAAABDv//+gAAAAIO///7gAAABA8P//CAEAABD2//9gAAAAAPf//9AAAABw9///7AAA
AAAAAAAUAAAAAAAAAAF6UgABeBABGwwHCJABAAAUAAAAHAAAAKj1//8mAAAAAEQHEAAAAAAkAAAA
NAAAAGDt//8wAQAAAA4QRg4YSg8LdwiAAD8aOSozJCIAAAAAFAAAAFwAAABo7v//EAAAAAAAAAAA
AAAAFAAAAHQAAABg7v//IAEAAAAAAAAAAAAAGAAAAIwAAAAo9v//ZQAAAABBDhCDAgJjDggAABgA
AACoAAAAfPb//0gAAAAARA4wfgoOCEELAABwAAAAxAAAADDv///OBQAAAEYOEI8CQg4YjgNCDiCN
BEIOKIwFQQ4whgZBDjiDB0oOoAUDggIKDjhDDjBBDihCDiBCDhhCDhBCDghBCwL2DqgFSA6wBUEO
uAVBDsAFZA6gBQKzDqgFRw6wBVIOqAVBDqAFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAMBkA
AAAAAADwGAAAAAAAAAEAAAAAAAAA8gAAAAAAAAAMAAAAAAAAAAAQAAAAAAAADQAAAAAAAAD4GQAA
AAAAABkAAAAAAAAAMD0AAAAAAAAbAAAAAAAAAAgAAAAAAAAAGgAAAAAAAAA4PQAAAAAAABwAAAAA
AAAACAAAAAAAAAD1/v9vAAAAALADAAAAAAAABQAAAAAAAABQBgAAAAAAAAYAAAAAAAAA4AMAAAAA
AAAKAAAAAAAAAI8BAAAAAAAACwAAAAAAAAAYAAAAAAAAABUAAAAAAAAAAAAAAAAAAAADAAAAAAAA
ADA/AAAAAAAAAgAAAAAAAACwAQAAAAAAABQAAAAAAAAABwAAAAAAAAAXAAAAAAAAAIgJAAAAAAAA
BwAAAAAAAACYCAAAAAAAAAgAAAAAAAAA8AAAAAAAAAAJAAAAAAAAABgAAAAAAAAAHgAAAAAAAAAI
AAAAAAAAAPv//28AAAAAAQAACAAAAAD+//9vAAAAABgIAAAAAAAA////bwAAAAABAAAAAAAAAPD/
/28AAAAA4AcAAAAAAAD5//9vAAAAAAMAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAPQAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAwEAAAAAAAAEAQAAAAAAAAUBAAAAAAAABgEAAAAAAAAHAQAAAA
AAAAgBAAAAAAAACQEAAAAAAAAKAQAAAAAAAAsBAAAAAAAADAEAAAAAAAANAQAAAAAAAA4BAAAAAA
AADwEAAAAAAAAAARAAAAAAAAEBEAAAAAAAAgEQAAAAAAADARAAAAAAAAQBEAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIQAAAAAAAAEdDQzogKFVi
dW50dSAxMy4zLjAtNnVidW50dTJ+MjQuMDQuMSkgMTMuMy4wAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAEAAAAEAPH/AAAAAAAAAAAAAAAAAAAAAAkAAAABAAQAjAMAAAAAAAAgAAAAAAAAABMA
AAAEAPH/AAAAAAAAAAAAAAAAAAAAAC0AAAACABAAQBkAAAAAAABlAAAAAAAAADoAAAACABAAsBkA
AAAAAABIAAAAAAAAAEEAAAAEAPH/AAAAAAAAAAAAAAAAAAAAAEwAAAACABAAgBgAAAAAAAAAAAAA
AAAAAE4AAAACABAAsBgAAAAAAAAAAAAAAAAAAGEAAAACABAA8BgAAAAAAAAAAAAAAAAAAHcAAAAB
ABoASEAAAAAAAAABAAAAAAAAAIMAAAABABYAOD0AAAAAAAAAAAAAAAAAAKoAAAACABAAMBkAAAAA
AAAAAAAAAAAAALYAAAABABUAMD0AAAAAAAAAAAAAAAAAAEEAAAAEAPH/AAAAAAAAAAAAAAAAAAAA
ANUAAAABABQAvCMAAAAAAAAAAAAAAAAAAAAAAAAEAPH/AAAAAAAAAAAAAAAAAAAAAOMAAAABABcA
QD0AAAAAAAAAAAAAAAAAAOwAAAAAABMAQCIAAAAAAAAAAAAAAAAAAP8AAAABABgAMD8AAAAAAAAA
AAAAAAAAABUBAAASAAAAAAAAAAAAAAAAAAAAAAAAADEBAAASAAAAAAAAAAAAAAAAAAAAAAAAAEwB
AAASAAAAAAAAAAAAAAAAAAAAAAAAAGABAAASAAAAAAAAAAAAAAAAAAAAAAAAAH0BAAASAAAAAAAA
AAAAAAAAAAAAAAAAAJoBAAAgAAAAAAAAAAAAAAAAAAAAAAAAALYBAAARABoAIEAAAAAAAAAIAAAA
AAAAAE0CAAAgABkAAEAAAAAAAAAAAAAAAAAAAMkBAAASAAAAAAAAAAAAAAAAAAAAAAAAAOIBAAAQ
ABkAEEAAAAAAAAAAAAAAAAAAAOkBAAASAhEA+BkAAAAAAAAAAAAAAAAAAO8BAAASAAAAAAAAAAAA
AAAAAAAAAAAAAAoCAAASAAAAAAAAAAAAAAAAAAAAAAAAABsCAAASAAAAAAAAAAAAAAAAAAAAAAAA
AC0CAAASAAAAAAAAAAAAAAAAAAAAAAAAAEsCAAAQABkAAEAAAAAAAAAAAAAAAAAAAFgCAAAgAAAA
AAAAAAAAAAAAAAAAAAAAAGcCAAARAhkACEAAAAAAAAAAAAAAAAAAAHQCAAARABIAACAAAAAAAAAE
AAAAAAAAAIMCAAASAAAAAAAAAAAAAAAAAAAAAAAAAJYCAAAQABoAUEAAAAAAAAAAAAAAAAAAAFEC
AAASABAAUBgAAAAAAAAmAAAAAAAAAJsCAAAQABoAIEAAAAAAAAAAAAAAAAAAAKcCAAASAAAAAAAA
AAAAAAAAAAAAAAAAALoCAAASABAAgBIAAAAAAADOBQAAAAAAAL8CAAASAAAAAAAAAAAAAAAAAAAA
AAAAANgCAAASAAAAAAAAAAAAAAAAAAAAAAAAAO0CAAASAAAAAAAAAAAAAAAAAAAAAAAAAP4CAAAS
AAAAAAAAAAAAAAAAAAAAAAAAABEDAAARAhkAEEAAAAAAAAAAAAAAAAAAAB0DAAASAAAAAAAAAAAA
AAAAAAAAAAAAADcDAAAgAAAAAAAAAAAAAAAAAAAAAAAAAFEDAAASAAAAAAAAAAAAAAAAAAAAAAAA
AGYDAAASAAAAAAAAAAAAAAAAAAAAAAAAAHcDAAAiAAAAAAAAAAAAAAAAAAAAAAAAAJIDAAASAgwA
ABAAAAAAAAAAAAAAAAAAAJgDAAARABoAQEAAAAAAAAAIAAAAAAAAAABTY3J0MS5vAF9fYWJpX3Rh
ZwBncDEwMF9jZmcxX3JhY2Vfb3B0ZmJpby5jAHBsYW50X2FjdGl2ZQBuc19ub3cAY3J0c3R1ZmYu
YwBkZXJlZ2lzdGVyX3RtX2Nsb25lcwBfX2RvX2dsb2JhbF9kdG9yc19hdXgAY29tcGxldGVkLjAA
X19kb19nbG9iYWxfZHRvcnNfYXV4X2ZpbmlfYXJyYXlfZW50cnkAZnJhbWVfZHVtbXkAX19mcmFt
ZV9kdW1teV9pbml0X2FycmF5X2VudHJ5AF9fRlJBTUVfRU5EX18AX0RZTkFNSUMAX19HTlVfRUhf
RlJBTUVfSERSAF9HTE9CQUxfT0ZGU0VUX1RBQkxFXwBfX2lzb2MyM19zdHJ0b3VsQEdMSUJDXzIu
MzgAX19zbnByaW50Zl9jaGtAR0xJQkNfMi4zLjQAcHV0Y2hhckBHTElCQ18yLjIuNQBfX2xpYmNf
c3RhcnRfbWFpbkBHTElCQ18yLjM0AF9fZXJybm9fbG9jYXRpb25AR0xJQkNfMi4yLjUAX0lUTV9k
ZXJlZ2lzdGVyVE1DbG9uZVRhYmxlAHN0ZG91dEBHTElCQ18yLjIuNQBjbG9ja19nZXR0aW1lQEdM
SUJDXzIuMTcAX2VkYXRhAF9maW5pAF9fc3RhY2tfY2hrX2ZhaWxAR0xJQkNfMi40AG1tYXBAR0xJ
QkNfMi4yLjUAY2xvc2VAR0xJQkNfMi4yLjUAc2NoZWRfc2V0YWZmaW5pdHlAR0xJQkNfMi4zLjQA
X19kYXRhX3N0YXJ0AF9fZ21vbl9zdGFydF9fAF9fZHNvX2hhbmRsZQBfSU9fc3RkaW5fdXNlZABm
Zmx1c2hAR0xJQkNfMi4yLjUAX2VuZABfX2Jzc19zdGFydABtdW5tYXBAR0xJQkNfMi4yLjUAbWFp
bgBfX3ByaW50Zl9jaGtAR0xJQkNfMi4zLjQAbWxvY2thbGxAR0xJQkNfMi4yLjUAb3BlbkBHTElC
Q18yLjIuNQBmd3JpdGVAR0xJQkNfMi4yLjUAX19UTUNfRU5EX18AX19mcHJpbnRmX2Noa0BHTElC
Q18yLjMuNABfSVRNX3JlZ2lzdGVyVE1DbG9uZVRhYmxlAHN0cmVycm9yQEdMSUJDXzIuMi41AGZz
dGF0QEdMSUJDXzIuMzMAX19jeGFfZmluYWxpemVAR0xJQkNfMi4yLjUAX2luaXQAc3RkZXJyQEdM
SUJDXzIuMi41AAAuc3ltdGFiAC5zdHJ0YWIALnNoc3RydGFiAC5pbnRlcnAALm5vdGUuZ251LnBy
b3BlcnR5AC5ub3RlLmdudS5idWlsZC1pZAAubm90ZS5BQkktdGFnAC5nbnUuaGFzaAAuZHluc3lt
AC5keW5zdHIALmdudS52ZXJzaW9uAC5nbnUudmVyc2lvbl9yAC5yZWxhLmR5bgAucmVsYS5wbHQA
LmluaXQALnBsdC5nb3QALnBsdC5zZWMALnRleHQALmZpbmkALnJvZGF0YQAuZWhfZnJhbWVfaGRy
AC5laF9mcmFtZQAuaW5pdF9hcnJheQAuZmluaV9hcnJheQAuZHluYW1pYwAuZGF0YQAuYnNzAC5j
b21tZW50AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
AAAAAAAAAAAAAAAAAAAAAAAbAAAAAQAAAAIAAAAAAAAAGAMAAAAAAAAYAwAAAAAAABwAAAAAAAAA
AAAAAAAAAAABAAAAAAAAAAAAAAAAAAAAIwAAAAcAAAACAAAAAAAAADgDAAAAAAAAOAMAAAAAAAAw
AAAAAAAAAAAAAAAAAAAACAAAAAAAAAAAAAAAAAAAADYAAAAHAAAAAgAAAAAAAABoAwAAAAAAAGgD
AAAAAAAAJAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAABJAAAABwAAAAIAAAAAAAAAjAMA
AAAAAACMAwAAAAAAACAAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAAVwAAAPb//28CAAAA
AAAAALADAAAAAAAAsAMAAAAAAAAwAAAAAAAAAAYAAAAAAAAACAAAAAAAAAAAAAAAAAAAAGEAAAAL
AAAAAgAAAAAAAADgAwAAAAAAAOADAAAAAAAAcAIAAAAAAAAHAAAAAQAAAAgAAAAAAAAAGAAAAAAA
AABpAAAAAwAAAAIAAAAAAAAAUAYAAAAAAABQBgAAAAAAAI8BAAAAAAAAAAAAAAAAAAABAAAAAAAA
AAAAAAAAAAAAcQAAAP///28CAAAAAAAAAOAHAAAAAAAA4AcAAAAAAAA0AAAAAAAAAAYAAAAAAAAA
AgAAAAAAAAACAAAAAAAAAH4AAAD+//9vAgAAAAAAAAAYCAAAAAAAABgIAAAAAAAAgAAAAAAAAAAH
AAAAAQAAAAgAAAAAAAAAAAAAAAAAAACNAAAABAAAAAIAAAAAAAAAmAgAAAAAAACYCAAAAAAAAPAA
AAAAAAAABgAAAAAAAAAIAAAAAAAAABgAAAAAAAAAlwAAAAQAAABCAAAAAAAAAIgJAAAAAAAAiAkA
AAAAAACwAQAAAAAAAAYAAAAYAAAACAAAAAAAAAAYAAAAAAAAAKEAAAABAAAABgAAAAAAAAAAEAAA
AAAAAAAQAAAAAAAAGwAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAACcAAAAAQAAAAYAAAAA
AAAAIBAAAAAAAAAgEAAAAAAAADABAAAAAAAAAAAAAAAAAAAQAAAAAAAAABAAAAAAAAAApwAAAAEA
AAAGAAAAAAAAAFARAAAAAAAAUBEAAAAAAAAQAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAQAAAAAAAA
ALAAAAABAAAABgAAAAAAAABgEQAAAAAAAGARAAAAAAAAIAEAAAAAAAAAAAAAAAAAABAAAAAAAAAA
EAAAAAAAAAC5AAAAAQAAAAYAAAAAAAAAgBIAAAAAAACAEgAAAAAAAHgHAAAAAAAAAAAAAAAAAAAQ
AAAAAAAAAAAAAAAAAAAAvwAAAAEAAAAGAAAAAAAAAPgZAAAAAAAA+BkAAAAAAAANAAAAAAAAAAAA
AAAAAAAABAAAAAAAAAAAAAAAAAAAAMUAAAABAAAAAgAAAAAAAAAAIAAAAAAAAAAgAAAAAAAAQAIA
AAAAAAAAAAAAAAAAAAgAAAAAAAAAAAAAAAAAAADNAAAAAQAAAAIAAAAAAAAAQCIAAAAAAABAIgAA
AAAAAEQAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAA2wAAAAEAAAACAAAAAAAAAIgiAAAA
AAAAiCIAAAAAAAA4AQAAAAAAAAAAAAAAAAAACAAAAAAAAAAAAAAAAAAAAOUAAAAOAAAAAwAAAAAA
AAAwPQAAAAAAADAtAAAAAAAACAAAAAAAAAAAAAAAAAAAAAgAAAAAAAAACAAAAAAAAADxAAAADwAA
AAMAAAAAAAAAOD0AAAAAAAA4LQAAAAAAAAgAAAAAAAAAAAAAAAAAAAAIAAAAAAAAAAgAAAAAAAAA
/QAAAAYAAAADAAAAAAAAAEA9AAAAAAAAQC0AAAAAAADwAQAAAAAAAAcAAAAAAAAACAAAAAAAAAAQ
AAAAAAAAAKsAAAABAAAAAwAAAAAAAAAwPwAAAAAAADAvAAAAAAAA0AAAAAAAAAAAAAAAAAAAAAgA
AAAAAAAACAAAAAAAAAAGAQAAAQAAAAMAAAAAAAAAAEAAAAAAAAAAMAAAAAAAABAAAAAAAAAAAAAA
AAAAAAAIAAAAAAAAAAAAAAAAAAAADAEAAAgAAAADAAAAAAAAACBAAAAAAAAAEDAAAAAAAAAwAAAA
AAAAAAAAAAAAAAAAIAAAAAAAAAAAAAAAAAAAABEBAAABAAAAMAAAAAAAAAAAAAAAAAAAABAwAAAA
AAAALQAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAQAAAAAAAAABAAAAAgAAAAAAAAAAAAAAAAAAAAAA
AABAMAAAAAAAAFgFAAAAAAAAHQAAABQAAAAIAAAAAAAAABgAAAAAAAAACQAAAAMAAAAAAAAAAAAA
AAAAAAAAAAAAmDUAAAAAAACrAwAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAABEAAAADAAAA
AAAAAAAAAAAAAAAAAAAAAEM5AAAAAAAAGgEAAAAAAAAAAAAAAAAAAAEAAAAAAAAAAAAAAAAAAAA=
__HELPER_B64_END__
