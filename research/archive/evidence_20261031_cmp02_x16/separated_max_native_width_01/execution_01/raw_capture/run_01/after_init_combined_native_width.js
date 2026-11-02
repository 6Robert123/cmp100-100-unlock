/* Offline candidate only: distinct MAX16 store, stable acceptance check, optional one command2. */
(function(){
  var quiet=false,quietRows=[];
  function emit(row){if(quiet){quietRows.push(row);return;}printRow(row);}
  function printRow(r){
    if(r.kind==="mark")Out.Printf(Out.PriHigh,"SEP16 %s\n",r.text);
    else if(r.kind==="read")Out.Printf(Out.PriHigh,"SEP16 READ phase=%s addr=0x%x type=%s value=%s valid=%s\n",r.phase,r.a,r.type,r.value,r.valid);
    else if(r.kind==="write")Out.Printf(Out.PriHigh,"SEP16 WRITE phase=%s addr=0x%x value=0x%x rc=%s status=%s error=%s\n",r.phase,r.a,r.v,r.rc,r.status,r.error);
  }
  function mark(s){emit({kind:"mark",text:s});}
  function flushQuiet(){quiet=false;for(var k=0;k<quietRows.length;k++)printRow(quietRows[k]);quietRows=[];}
  function stop(s){if(quiet)flushQuiet();mark("STOP reason="+s);return 0;}
  function u32(x){return x>>>0;}
  function rd(a,phase){
    try{
      var raw=s.RegRd32(a),t=typeof raw;
      var valid=t==="number"&&isFinite(raw)&&Math.floor(raw)===raw&&raw>=-2147483648&&raw<=4294967295;
      var v=valid?u32(raw):0;
      valid=valid&&v!==0xffffffff;
      emit({kind:"read",phase:phase,a:a,type:t,value:valid?"0x"+v.toString(16):String(raw),valid:String(valid)});
      return valid?{ok:true,v:v}:{ok:false};
    }catch(e){mark("READ_EXCEPTION phase="+phase+" addr=0x"+a.toString(16)+" error="+String(e));return {ok:false};}
  }
  function wr(a,v,phase){
    var r;
    try{r=s.RegWr32(a,u32(v));}
    catch(e){emit({kind:"write",phase:phase,a:a,v:u32(v),rc:"unknown",status:"exception",error:String(e)});return false;}
    var t=typeof r;
    var ok=(r!==null&&typeof r!=="undefined"&&(t==="number"||t==="object")&&!(r!=OK));
    emit({kind:"write",phase:phase,a:a,v:u32(v),rc:String(r),status:ok?"OK":"nonzero_or_invalid_rc",error:"none"});
    return ok;
  }
  function gate(){
    var f,rc,x;
    try{f=new InteractiveFile();rc=f.Open("/work/probe_gate.txt");if(rc!=OK)return "OPEN_ERROR";x=f.Read();f.Close();}
    catch(e){try{f.Close();}catch(z){}return "IO_ERROR";}
    if(typeof x!=="string")return "BAD_TYPE";x=x.replace(/\n$/ ,"");
    return x==="WAIT"||x==="GO"||x==="STOP"?x:"BAD_CONTENT";
  }
  function validBdf(x){return x&&Number(x.PciDomainNumber)===0&&Number(x.PciBusNumber)===2&&Number(x.PciDeviceNumber)===0&&Number(x.PciFunctionNumber)===0;}
  function statusClass(q){
    if(!q.ok)return "UNKNOWN";
    var st=(q.v>>>16)&0xffff, speed=st&15, width=(st>>>4)&63, training=!!(st&(1<<11));
    if(speed===1&&width===1&&!training)return "GEN1_X1";
    if(speed===1&&width===16&&!training)return "GEN1_X16";
    return "OTHER";
  }
  function restoreCya(){
    var q=rd(0x88150,"cya29_terminal_precheck");
    if(!q.ok)return {ok:false,reason:"cya_precheck_unknown"};
    var target=u32(q.v|0x20000000);
    if(!wr(0x88150,target,"cya29_terminal_restore"))return {ok:false,reason:"cya_restore_write_failed"};
    var rb=rd(0x88150,"cya29_terminal_readback");
    if(!rb.ok||rb.v!==target)return {ok:false,reason:"cya_restore_readback_mismatch"};
    return {ok:true,value:target};
  }
  function holdAfterCleanup(result){
    flushQuiet();
    if(!result.ok){mark("TERMINAL status=CYA_CLEANUP_UNKNOWN reason="+result.reason);return 0;}
    mark("CYA_RESTORE status=OK value=0x"+result.value.toString(16));
    mark("TERMINAL status=CLEANUP_KNOWN");
    mark("HOLD_BEGIN ms=20000 no_gpu_io=true");
    var until=(new Date()).getTime()+20000;
    while((new Date()).getTime()<until){}
    mark("HOLD_END");
    return 0;
  }
  function rejectMax(reason){
    mark("MAX_DECISION status=REJECTED reason="+reason+" command2=SKIPPED");
    return holdAfterCleanup(restoreCya());
  }
  var s;
  try{
    if(Gpu.IsInitialized!==true||GpuDevMgr.IsInitialized!==true||GpuDevMgr.NumGpus!==1)return stop("gpu_guard");
    s=GpuDevMgr.GetGpuSubdeviceByGpuIdx(0);
    if(!validBdf(s))return stop("exact_bdf");
    if(typeof s.RegRd32!=="function"||typeof s.RegWr32!=="function")return stop("register_api_missing");
  }catch(e){return stop("init_exception:"+String(e));}

  var q,reads=[
    [0x88000,0x15c210de,"gpu_identity"],
    [0x88084,0x00453c11,"initial_link_cap"],
    [0x8c040,0x08089000,"initial_link_word"],
    [0x88150,0xe0000195,"initial_cya"],
    [0x8b980,null,"outerprep_word"],
    [0x88088,null,"initial_link_ctl_sta"]
  ];
  var state={};
  for(var i=0;i<reads.length;i++){
    q=rd(reads[i][0],"baseline_"+reads[i][2]);
    if(!q.ok)return stop("baseline_unknown_"+reads[i][2]);
    state[reads[i][2]]=q.v;
    if(reads[i][1]!==null&&q.v!==reads[i][1])return stop("baseline_mismatch_"+reads[i][2]);
  }
  if((state.outerprep_word&0x00ff0000)!==0)return stop("outerprep_bits23_16_not_zero");
  if(((state.initial_cya>>>7)&3)!==3)return stop("cya_bits7_8_not_both_set");
  if(statusClass({ok:true,v:state.initial_link_ctl_sta})!=="GEN1_X1")return stop("initial_endpoint_not_gen1_x1");
  if(gate()!=="WAIT")return stop("gate_warmup_not_WAIT");
  mark("WAIT_BEGIN bdf=0000:02:00.0");
  var start=(new Date()).getTime(),g="TIMEOUT",n=0;
  while(n<300&&(new Date()).getTime()-start<30000){
    g=gate();n++;if(g!=="WAIT")break;
    var until=(new Date()).getTime()+100;while((new Date()).getTime()<until){}
  }
  mark("GATE decision="+g+" polls="+n);if(g!=="GO")return stop("gate_"+g);
  for(var j=0;j<reads.length;j++){
    q=rd(reads[j][0],"prewrite_"+reads[j][2]);
    if(!q.ok)return stop("prewrite_unknown_"+reads[j][2]);
    if(reads[j][1]!==null&&q.v!==reads[j][1])return stop("prewrite_mismatch_"+reads[j][2]);
    state[reads[j][2]]=q.v;
  }
  if((state.outerprep_word&0x00ff0000)!==0||((state.initial_cya>>>7)&3)!==3||statusClass({ok:true,v:state.initial_link_ctl_sta})!=="GEN1_X1")return stop("prewrite_outerprep_cya_or_linkstatus_changed");

  quiet=true;quietRows=[];
  var nativeBase=u32(state.initial_link_word&0xff8fffff);
  if(nativeBase!==0x08089000)return stop("native_clear_arithmetic_guard");
  if(!wr(0x8c040,nativeBase,"native_22_20_clear"))return rejectMax("native_clear_write_failed");
  q=rd(0x8c040,"native_22_20_clear_readback");
  if(!q.ok||q.v!==nativeBase)return rejectMax("native_clear_readback_mismatch");

  var cyaClear=u32(state.initial_cya&0xdfffffff);
  if(cyaClear!==0xc0000195)return stop("cya_clear_arithmetic_guard");
  if(!wr(0x88150,cyaClear,"cya29_clear"))return holdAfterCleanup(restoreCya());
  q=rd(0x88150,"cya29_clear_readback");
  if(!q.ok||q.v!==cyaClear)return holdAfterCleanup(restoreCya());

  var maxTarget=u32((nativeBase&0x07ffffff)|0x80000000);
  if(maxTarget!==0x80089000)return rejectMax("max16_arithmetic_guard");
  if(!wr(0x8c040,maxTarget,"separate_max16_command0"))return rejectMax("max16_write_failed");
  q=rd(0x8c040,"max16_exact_readback");
  if(!q.ok||q.v!==maxTarget)return rejectMax("max16_exact_readback_rejected");
  var cap=rd(0x88084,"max16_cap_readback");
  if(!cap.ok||cap.v!==0x00453d01)return rejectMax("max16_linkcap_not_00453d01");

  var stable=true;
  for(var p=0;p<3;p++){
    if(p>0){var waitUntil=(new Date()).getTime()+100;while((new Date()).getTime()<waitUntil){}}
    var word=rd(0x8c040,"max16_stability_word_"+p);
    var c=rd(0x88084,"max16_stability_cap_"+p);
    if(!word.ok||!c.ok||word.v!==maxTarget||c.v!==0x00453d01)stable=false;
  }
  if(!stable)return rejectMax("max16_cap_or_word_not_stable");
  mark("MAX_STABILITY status=PASS pairs=3 min_spacing_ms=100");

  var link=rd(0x88088,"post_max_link_ctl_sta"),linkState=statusClass(link);
  if(!link.ok){mark("MAX_DECISION status=UNKNOWN_LINKSTA command2=SKIPPED");return holdAfterCleanup(restoreCya());}
  if(linkState==="GEN1_X16"){
    mark("MAX_DECISION status=EP_ALREADY_GEN1_X16 command2=SKIPPED");
    return holdAfterCleanup(restoreCya());
  }
  if(linkState!=="GEN1_X1")return rejectMax("endpoint_not_gen1_x1_after_max");

  var preWord=rd(0x8c040,"command2_precheck_word"),preCap=rd(0x88084,"command2_precheck_cap"),preLink=rd(0x88088,"command2_precheck_link_ctl_sta");
  if(!preWord.ok||!preCap.ok||!preLink.ok)return rejectMax("command2_precheck_unknown");
  if(preCap.v!==0x00453d01||preWord.v!==maxTarget)return rejectMax("command2_precheck_max_or_idle_changed");
  var lastState=statusClass(preLink);
  if(lastState==="GEN1_X16"){
    mark("MAX_DECISION status=EP_ALREADY_GEN1_X16 command2=SKIPPED");
    return holdAfterCleanup(restoreCya());
  }
  if(lastState!=="GEN1_X1")return rejectMax("command2_precheck_endpoint_not_gen1_x1");
  var cmdTarget=u32((preWord.v&0xffffffe0)|2);
  if(cmdTarget!==0x80089002)return rejectMax("command2_arithmetic_guard");
  if(!wr(0x8c040,cmdTarget,"native_command2_once"))return holdAfterCleanup({ok:false,reason:"command2_write_failed_state_unknown"});
  mark("MAX_DECISION status=MAX16_STABLE_ENDPOINT_X1 command2=ISSUED_ONCE");

  q=rd(0x8c040,"command2_immediate_readback");
  if(!q.ok)return holdAfterCleanup({ok:false,reason:"command2_immediate_read_unknown_no_guessed_cleanup"});
  var done=(q.v&0x1f)===0,polls=0,deadline=(new Date()).getTime()+4000;
  while(!done&&(new Date()).getTime()<deadline&&polls<40){
    var pause=(new Date()).getTime()+100;while((new Date()).getTime()<pause){}
    q=rd(0x8c040,"command2_busy_poll_"+polls);polls++;
    if(!q.ok)return holdAfterCleanup({ok:false,reason:"command2_busy_read_unknown_no_guessed_cleanup"});
    done=(q.v&0x1f)===0;
  }
  if(done){mark("NATIVE_COMMAND status=COMPLETE polls="+polls);return holdAfterCleanup(restoreCya());}
  mark("NATIVE_COMMAND status=TIMEOUT polls="+polls+" window_ms=4000");
  return holdAfterCleanup(restoreCya());
})();
