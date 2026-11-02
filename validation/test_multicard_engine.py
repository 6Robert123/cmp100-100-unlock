#!/usr/bin/env python3
"""CPU-only checks of the actual embedded controller. No helper or GPU execution."""
import copy
import pathlib
import re
import sys
import tempfile
import types
import unittest
from unittest import mock
sys.dont_write_bytecode=True
ROOT=pathlib.Path(__file__).resolve().parent.parent
SCRIPT=ROOT/'unlock/cmp100-12gb.sh'
SOURCE=SCRIPT.read_text().split("<<'PY_ENGINE'\n",1)[1].split('\nPY_ENGINE\n',1)[0]
engine=types.ModuleType('cmp100_test_engine');sys.modules[engine.__name__]=engine
exec(compile(SOURCE,str(SCRIPT)+'[embedded Python]','exec'),engine.__dict__)
Device=engine.Device

class FakeBackend:
    """In-memory PCI/driver model. Does not invoke any host command or helper."""
    def __init__(self,count=3,mixed=True,cold=False,fail=None):
        self.devices=[Device(f'0000:{i+1:02x}:00.0',True,None if cold else 'nvidia') for i in range(count)]
        if mixed:self.devices += [Device('0000:40:00.0',False,'nvidia'),Device('0000:50:00.0',False,'amdgpu'),Device('0000:60:00.0',False,'nvidia')]
        self.drivers={d.bdf:d.driver for d in self.devices};self.initial=copy.deepcopy(self.drivers)
        self.cap={d.bdf:(6144 if d.target else 16384) for d in self.devices}
        self.events=[];self.fail=fail;self.loaded=not cold;self.live=None;self.protected={};self.partial=set()
    def event(self,name,bdf=None):
        self.events.append((name,bdf))
        if self.fail==(name,bdf) or self.fail==name:raise RuntimeError('fixture failure '+name)
    def inventory(self):return self.devices
    def preflight(self,targets,**kwargs):self.targets={d.bdf for d in targets};self.event('preflight')
    def acquire_lock(self):self.event('lock')
    def release_lock(self):self.event('unlock')
    def begin(self,targets):self.event('begin')
    def load_driver_once(self):
        self.event('load_once')
        if not self.loaded:
            self.loaded=True
            for d in self.devices:
                if d.driver is None:self.drivers[d.bdf]='nvidia'
    def capture_protected(self,targets):
        self.protected={b:dr for b,dr in self.drivers.items() if b not in self.targets};self.event('capture_protected')
    def prepare(self,d):
        self.partial.add(d.bdf);self.event('prepare',d.bdf)
        assert d.bdf in self.targets
        self.drivers[d.bdf]=None;self.event('reset',d.bdf)
    def arm(self,d):self.event('arm',d.bdf);self.live=d.bdf;return d.bdf
    def bind(self,d):
        assert self.live==d.bdf,'bind before arm'
        self.event('bind',d.bdf);self.drivers[d.bdf]='nvidia'
    def probe_target(self,d,w):
        assert self.live==d.bdf
        self.event('query_live',d.bdf);self.cap[d.bdf]=12288
    def wait_helper(self,d,w):self.event('wait_hit',d.bdf)
    def stop_helper(self,w):self.event('stop',w);self.live=None
    def target_query(self,d):
        self.event('query',d.bdf)
        if self.cap[d.bdf]!=12288:raise RuntimeError('bad capacity')
    def final_query(self,targets):
        self.event('final_query')
        return {d.bdf:self.cap[d.bdf] for d in targets}
    def assert_success(self,targets,result):
        self.event('assert_success')
        if set(result)!=self.targets or any(v!=12288 for v in result.values()):raise RuntimeError('final mismatch')
        if any(self.drivers[b]!=dr for b,dr in self.protected.items()):raise RuntimeError('protected changed')
    def cleanup(self,changed):
        self.cleaned=[d.bdf for d in changed];self.event('cleanup')
        assert self.partial.issubset(self.cleaned),'partial prepare omitted'
    def verify_final_bindings(self,targets):
        self.event('verify_bindings')
        assert all(self.drivers[d.bdf]=='nvidia' for d in targets)
        assert all(self.drivers[b]==dr for b,dr in self.protected.items())
    def log(self,text):self.events.append(('log',text))
    def report_failure(self,e,changed):self.events.append(('failure',str(e)))

class MultiCardControllerTests(unittest.TestCase):
    def run_controller(self,b,mode='apply',cold=False,targets=None):
        targets=targets or [d for d in b.devices if d.target]
        return engine.Controller(b,mode,cold).run(targets)
    def assert_no_ready(self,b):self.assertFalse(any('CMP100_12GB_READY' in str(v) for k,v in b.events))
    def test_three_cmp_p100_rtx_amd_preserved(self):
        b=FakeBackend();result=self.run_controller(b)
        self.assertEqual(len(result),3)
        for d in b.devices:
            if not d.target:self.assertEqual(b.initial[d.bdf],b.drivers[d.bdf])
        for operation,bdf in b.events:
            if operation in ('prepare','reset','arm','bind','query_live','wait_hit','query'):self.assertIn(bdf,b.targets)
        for bdf in b.targets:
            names=[n for n,b in b.events if b==bdf]
            self.assertLess(names.index('arm'),names.index('bind'))
            self.assertLess(names.index('query_live'),names.index('wait_hit'))
    def test_sixteen_cmp_mixed(self):
        b=FakeBackend(count=16);self.assertEqual(len(self.run_controller(b)),16)
    def test_selected_subset_leaves_other_cmp_untouched(self):
        b=FakeBackend();self.run_controller(b,targets=[b.devices[1]])
        self.assertEqual({x for n,x in b.events if n=='reset'},{b.devices[1].bdf})
        self.assertEqual(b.cap[b.devices[0].bdf],6144)
    def test_boot_load_once_and_no_unload(self):
        b=FakeBackend(cold=True);self.run_controller(b,cold=True)
        self.assertEqual(sum(n=='load_once' for n,_ in b.events),1)
        self.assertFalse(any('unload' in n for n,_ in b.events))
    def test_check_no_module_helper_reset_query_or_lock(self):
        b=FakeBackend();self.run_controller(b,mode='check')
        self.assertEqual(b.events,[('preflight',None)])
    def test_first_and_later_failure_stops_without_next_reset(self):
        for index in (0,1,2):
            for phase in ('prepare','reset','arm','bind','query_live','wait_hit','query'):
                with self.subTest(index=index,phase=phase):
                    b=FakeBackend();b.fail=(phase,b.devices[index].bdf)
                    with self.assertRaises(RuntimeError):self.run_controller(b)
                    self.assert_no_ready(b)
                    attempted=[x for n,x in b.events if n=='prepare']
                    self.assertEqual(attempted,[d.bdf for d in b.devices[:index+1]])
                    self.assertIn(b.devices[index].bdf,b.cleaned)
                    self.assertIsNone(b.live)
    def test_partial_prepare_and_cleanup_failure_report_nonzero(self):
        b=FakeBackend(fail='cleanup')
        with self.assertRaisesRegex(RuntimeError,'cleanup'):self.run_controller(b)
        self.assert_no_ready(b)
    def test_final_changed_capacity_never_ready(self):
        b=FakeBackend()
        original=b.final_query
        def bad(targets):
            result=original(targets);result[targets[0].bdf]=6144;return result
        b.final_query=bad
        with self.assertRaises(RuntimeError):self.run_controller(b)
        self.assert_no_ready(b)
    def test_final_protected_binding_change_never_ready(self):
        b=FakeBackend()
        orig=b.final_query
        def bad(targets):
            b.drivers['0000:40:00.0']=None;return orig(targets)
        b.final_query=bad
        with self.assertRaises(RuntimeError):self.run_controller(b)
        self.assert_no_ready(b)
    def test_csv_exact_identity_and_capacity_shape(self):
        b='0000:01:00.0'
        self.assertEqual(engine.parse_csv('00000000:01:00.0, 12288\n',[b]),{b:12288})
        for text in ['0000:02:00.0,12288\n','0000:01:00.0,12288\n0000:01:00.0,12288\n','0000:01:00.0,N/A\n','']:
            with self.subTest(text=text),self.assertRaises(RuntimeError):engine.parse_csv(text,[b])
    def test_null_override_sentinels(self):
        for v in ('','\n','(null)','(null)\n'):self.assertEqual(engine.null_override(v),'')
        self.assertEqual(engine.null_override('vfio-pci\n'),'vfio-pci')
    def test_exact_helper_payload_and_bad_hash_rejected(self):
        blob=engine.helper_bytes(SCRIPT.read_bytes())
        self.assertEqual(blob,(ROOT/'unlock/historical/gp100_cfg1_race.x86_64.elf').read_bytes())
        with self.assertRaises(Exception):engine.helper_bytes(b'__HELPER_B64_BEGIN__\nYQ==\n__HELPER_B64_END__')
    def test_linux_write_allowlist_blocks_non_target_and_global_reset(self):
        backend=engine.LinuxBackend(SCRIPT);backend.allowed={'0000:01:00.0'}
        backend.log=lambda x:None;commands=[];backend.cmd=lambda args,**kw:commands.append(args) or ''
        for p,value in [(engine.SYS/'0000:40:00.0/reset','1\n'),(engine.NVIDIA/'unbind','0000:40:00.0\n'),(pathlib.Path('/sys/bus/pci/rescan'),'1\n')]:
            with self.assertRaises(RuntimeError):backend.write(p,value)
        self.assertEqual(commands,[])
        backend.write(engine.NVIDIA/'bind','0000:01:00.0\n');self.assertEqual(len(commands),1)
    def test_boot_plan_contains_no_start_now_or_blacklist(self):
        plan=engine.install_plan(SCRIPT.read_bytes())
        self.assertEqual(len(plan),2)
        unit=plan[1][1].decode();self.assertIn('--boot',unit);self.assertIn('Before=multi-user.target',unit)
        self.assertNotIn('modprobe -r',unit);self.assertNotIn('blacklist',unit)
    def test_cli_modes_reject_duplicates_and_non_cmp(self):
        b=FakeBackend()
        with self.assertRaises(RuntimeError):engine.main(['--apply','0000:40:00.0'],lambda _:b,str(SCRIPT))
        with self.assertRaises(RuntimeError):engine.main(['--apply','0000:01:00.0','0000:01:00.0'],lambda _:b,str(SCRIPT))
        import contextlib,io
        with contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):engine.parse(['--check','--apply'])


class LinuxBoundaryTests(unittest.TestCase):
    def setUp(self):
        import io
        quiet=mock.patch('sys.stdout',new=io.StringIO());quiet.start();self.addCleanup(quiet.stop)
    def test_null_cleanup_writes_empty_override_not_literal_sentinel(self):
        for old in ('(null)\n','\n',''):
            backend=engine.LinuxBackend(SCRIPT);d=Device('0000:01:00.0',True,'nvidia')
            backend.allowed={d.bdf};backend.log=lambda _:None;calls=[]
            backend.write=lambda p,v:calls.append((str(p),v))
            backend.states={d.bdf:{'override':old,'methods':'bus\n','driver':'nvidia','reset_started':True,'override_written':True,'methods_written':True,'detached':True}}
            backend.cleanup([d])
            self.assertIn((str(engine.SYS/d.bdf/'driver_override'),'\n'),calls)
    def test_owner_errors_and_ambiguous_output_fail_closed(self):
        backend=engine.LinuxBackend(SCRIPT)
        for rc,out,err,accepted in [(1,'','',True),(0,'123 456','',True),(2,'','error',False),(1,'','denied',False),(0,'unknown','',False)]:
            r=types.SimpleNamespace(returncode=rc,stdout=out,stderr=err)
            with mock.patch.object(engine.pathlib.Path,'exists',return_value=True),mock.patch.object(engine.subprocess,'run',return_value=r):
                if accepted:backend.fuser_pids('/FAKE-NODE')
                else:
                    with self.assertRaises(RuntimeError):backend.fuser_pids('/FAKE-NODE')
    def test_interception_finishes_during_bind_before_nvml(self):
        backend=engine.LinuxBackend(SCRIPT);backend.log=lambda _:None
        with tempfile.TemporaryDirectory() as td:
            path=pathlib.Path(td)/'helper.log';path.write_text('ARMED trigger=fixture\nHIT_NS=fixture\n')
            for rc,accepted in [(0,True),(1,False)]:
                proc=types.SimpleNamespace(poll=lambda:rc);seen=[];backend.capacity=lambda d:seen.append(d.bdf)
                d=Device('0000:01:00.0',True)
                if accepted:backend.probe_target(d,(proc,None,path));self.assertEqual(seen,[d.bdf])
                else:
                    with self.assertRaises(RuntimeError):backend.probe_target(d,(proc,None,path))
    def test_post_cleanup_verification_is_reported_as_failure(self):
        b=FakeBackend(fail='verify_bindings')
        with self.assertRaises(RuntimeError):engine.Controller(b,'apply').run([d for d in b.devices if d.target])
        self.assertTrue(any(n=='failure' for n,_ in b.events))
        self.assertFalse(any('CMP100_12GB_READY' in str(v) for _,v in b.events))
    def installer_fixture(self,td):
        state={'enabled':False,'fail_enable':False,'commands':[]}
        def run(args,**kw):
            op=args[1];state['commands'].append(tuple(args[1:]))
            rc=0;out=''
            if args[0]=='getconf':out='glibc 2.39\n'
            elif op=='show':out='0\n'
            elif op=='is-enabled':rc=0 if args[-1]==unit.name and state['enabled'] else 1
            elif op=='enable':
                state['enabled']=True
                if state['fail_enable']:rc=1;state['fail_enable']=False
            elif op=='disable':state['enabled']=False
            return types.SimpleNamespace(returncode=rc,stdout=out,stderr='fixture')
        script=pathlib.Path(td)/'sbin/initializer';unit=pathlib.Path(td)/'units/initializer.service'
        patches=[mock.patch.object(engine,'INSTALL_SCRIPT',script),mock.patch.object(engine,'INSTALL_UNIT',unit),mock.patch.object(engine.platform,'system',return_value='Linux'),mock.patch.object(engine.platform,'machine',return_value='x86_64'),mock.patch.object(engine.os,'geteuid',return_value=0),mock.patch.object(engine.shutil,'which',return_value='/FAKE-systemctl'),mock.patch.object(engine.subprocess,'run',side_effect=run)]
        return state,script,unit,patches
    def test_boot_install_enable_without_start_and_uninstall(self):
        import contextlib
        with tempfile.TemporaryDirectory() as td,contextlib.ExitStack() as stack:
            state,script,unit,patches=self.installer_fixture(td)
            for patch in patches:stack.enter_context(patch)
            engine.install_boot(SCRIPT)
            self.assertTrue(script.exists() and unit.exists() and state['enabled'])
            self.assertFalse(any('start' in x or '--now' in x or 'stop' in x for x in state['commands']))
            engine.install_boot(SCRIPT,remove=True)
            self.assertFalse(script.exists() or unit.exists() or state['enabled'])
    def test_partial_enable_failure_rolls_back_files_and_symlink_state(self):
        import contextlib
        with tempfile.TemporaryDirectory() as td,contextlib.ExitStack() as stack:
            state,script,unit,patches=self.installer_fixture(td)
            for patch in patches:stack.enter_context(patch)
            state['fail_enable']=True
            with self.assertRaisesRegex(RuntimeError,'rollback completed'):engine.install_boot(SCRIPT)
            self.assertFalse(script.exists() or unit.exists() or state['enabled'])
    def test_uninstall_failure_restores_prior_enabled_state_and_files(self):
        import contextlib
        with tempfile.TemporaryDirectory() as td,contextlib.ExitStack() as stack:
            state,script,unit,patches=self.installer_fixture(td)
            for patch in patches:stack.enter_context(patch)
            engine.install_boot(SCRIPT);before=(script.read_bytes(),unit.read_bytes())
            unlink=engine.pathlib.Path.unlink;failed=[False]
            def fail_once(path,*args,**kwargs):
                if path==unit and not failed[0]:failed[0]=True;raise OSError('fixture removal failure')
                return unlink(path,*args,**kwargs)
            with mock.patch.object(engine.pathlib.Path,'unlink',fail_once),self.assertRaises(RuntimeError):engine.install_boot(SCRIPT,remove=True)
            self.assertTrue(state['enabled']);self.assertEqual((script.read_bytes(),unit.read_bytes()),before)

    def test_capacity_and_final_values_retained_in_logs(self):
        backend=engine.LinuxBackend(SCRIPT);messages=[];backend.log=messages.append
        backend.cmd=lambda args,**kwargs:'00000000:01:00.0, 12288\n'
        d=Device('0000:01:00.0',True)
        self.assertEqual(backend.final_query([d]),{d.bdf:12288})
        self.assertTrue(any('CAPACITY '+d.bdf+' memory_mib=12288' in x for x in messages))
        self.assertTrue(any(x.startswith('FINAL_CAPACITIES ') for x in messages))
    def test_uninstall_does_not_require_runtime_gpu_tools(self):
        import contextlib
        with tempfile.TemporaryDirectory() as td,contextlib.ExitStack() as stack:
            state,script,unit,patches=self.installer_fixture(td)
            for patch in patches:stack.enter_context(patch)
            engine.install_boot(SCRIPT)
            with mock.patch.object(engine.shutil,'which',side_effect=lambda name:'/FAKE-systemctl' if name=='systemctl' else None):
                engine.install_boot(SCRIPT,remove=True)
            self.assertFalse(script.exists() or unit.exists() or state['enabled'])

class ExecutableWorkdirTests(unittest.TestCase):
    def test_noexec_first_directory_falls_back_and_cleans_probe(self):
        with tempfile.TemporaryDirectory() as td:
            parents=[pathlib.Path(td)/name for name in ('run','var-tmp')]
            for p in parents:p.mkdir()
            backend=engine.LinuxBackend(SCRIPT)
            attempts=[]
            def execute(args,timeout):
                attempts.append(pathlib.Path(args[0]))
                self.assertEqual(timeout,3)
                if len(attempts)==1:raise PermissionError('noexec fixture')
                self.assertEqual(attempts[-1].read_text(),'#!/bin/sh\nexit 0\n')
            with mock.patch.object(backend,'cmd',side_effect=execute):
                work=backend.executable_workdir(parents)
            self.assertEqual(work.parent,parents[1])
            self.assertEqual(list(parents[0].iterdir()),[])
            self.assertEqual(list(work.iterdir()),[])
            self.assertEqual(work.stat().st_mode & 0o777,0o700)
    def test_all_noexec_fails_before_prepare_and_leaves_no_directories(self):
        with tempfile.TemporaryDirectory() as td:
            backend=engine.LinuxBackend(SCRIPT)
            with mock.patch.object(backend,'cmd',side_effect=PermissionError('noexec fixture')):
                with self.assertRaisesRegex(RuntimeError,'before GPU changes'):
                    backend.executable_workdir([td])
            self.assertEqual(list(pathlib.Path(td).iterdir()),[])
            self.assertEqual(backend.states,{})

if __name__=='__main__':unittest.main()
