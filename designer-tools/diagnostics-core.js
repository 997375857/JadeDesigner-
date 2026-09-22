export function createTrace(limit=200,clock=()=>performance.now(),changed=()=>{}) {
  let sequence=0;
  const rows=[];
  const safeChannel=value=>value===undefined?'':String(value);
  const serialize=value=>{
    if(value===undefined)return '';
    if(typeof value==='string')return value;
    try {
      const seen=new WeakSet();
      return JSON.stringify(value,(key,item)=>{
        if(item&&typeof item==='object'){
          if(seen.has(item))return '[循环引用]';
          seen.add(item);
        }
        return item;
      });
    } catch { return '[参数不可序列化]'; }
  };
  return {
    rows,
    clear(){rows.length=0;},
    start(channel,payload,mode='runtime') {
      const row={id:++sequence,channel:safeChannel(channel),payload:serialize(payload),response:'',error:'',mode,started:clock(),elapsed:0,state:'pending'};
      rows.push(row);if(rows.length>limit)rows.splice(0,rows.length-limit);
      try {changed(row);} catch {} return row;
    },
    finish(row,state,value){row.state=state;row.elapsed=Math.max(0,clock()-row.started);if(state==='response'||state==='unverified')row.response=serialize(value);else if(state==='rejected'||state==='threw')row.error=serialize(value);try {changed(row);} catch {}},
  };
}
export function wrapInvoke(original,trace,isPreview=()=>false) {
  return function(...args) {
    const row=trace.start(args[0],args[1],isPreview()?'preview':'runtime');
    row.replay=()=>Reflect.apply(original,this,args);
    let result;
    try {result=Reflect.apply(original,this,args);}
    catch(error){trace.finish(row,'threw',error?.message||error);throw error;}
    // Preserve arguments, receiver, fulfillment value and rejection reason.
    // Do not stringify payloads/results or read their properties for diagnostics.
    if(!(result instanceof Promise)) {trace.finish(row,'unverified',result);return result;}
    return result.then(value=>{trace.finish(row,'response',value);return value;},error=>{trace.finish(row,'rejected',error?.message||error);throw error;});
  };
}
