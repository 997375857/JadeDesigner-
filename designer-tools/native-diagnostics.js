import {createTrace,wrapInvoke} from './diagnostics-core.js';

// Keep the transport independent from the page's business handlers.
export function createNativeDiagnostics(send, metadata, isPreview, clock=()=>performance.now()) {
  const identities=new Map();
  const trace=createTrace(200,clock,row=>{
    if(row.state==='pending') {
      identities.set(row.id,metadata(row.channel)||{});
      if(identities.size>200)identities.delete(identities.keys().next().value);
    }
    const identity=identities.get(row.id)||{};
    const states={pending:'等待应答',response:'应答已到达',rejected:'调用失败',threw:'调用异常',unverified:'同步返回（未核验）'};
    const state=(row.mode==='preview'?'预览模拟（未调用易语言） · ':'实际调用 · ')+(states[row.state]||row.state);
    send('JADE_DOCK_TRACE\t'+[row.id,identity.handler||'',row.channel,row.payload,
      row.error||row.response,state,identity.assembly||'',Math.round(row.elapsed)+' ms']
      .map(value=>encodeURIComponent(String(value))).join('\t'));
  });
  return {trace,wrap:original=>wrapInvoke(original,trace,isPreview)};
}
