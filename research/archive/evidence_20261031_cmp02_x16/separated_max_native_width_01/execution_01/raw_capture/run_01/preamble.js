if(typeof g_CheckVbiosSecurity!=="boolean") throw new Error("policy type guard");
g_CheckVbiosSecurity=false;
(function(){
  var x,typ,length="unavailable",count=0,truncated=false;
  try{x=g_RegWrMask;}catch(e){Out.Printf(Out.PriHigh,"GATED_REGWRMASK status=property_exception error=%s\n",String(e));return 0;}
  typ=typeof x;
  try{if(x!==null && typeof x.length!=="undefined")length=String(x.length);}catch(e){length="length_exception";}
  if(x!==null && (typ==="object" || typ==="function")){
    try{for(var k in x){if(count>=256){truncated=true;break;}var v;try{v=x[k];}catch(e){Out.Printf(Out.PriHigh,"GATED_REGWRMASK_ENTRY key=%s status=read_exception\n",String(k));count++;continue;}Out.Printf(Out.PriHigh,"GATED_REGWRMASK_ENTRY key=%s type=%s value=%s\n",String(k),typeof v,(v===null || typeof v==="string" || typeof v==="number" || typeof v==="boolean")?String(v):"<non_primitive>");count++;}}
    catch(e){Out.Printf(Out.PriHigh,"GATED_REGWRMASK_ENUM_EXCEPTION error=%s\n",String(e));}
  }
  Out.Printf(Out.PriHigh,"GATED_REGWRMASK type=%s length=%s entries=%d truncated=%s\n",typ,length,count,String(truncated));
  return 0;
})();
