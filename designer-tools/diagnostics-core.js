export function createTrace(limit=200,clock=()=>performance.now()) {
  let sequence=0;
  const rows=[];
  const safeChannel=value=>typeof value==='string'&&/^[\p{L}_][\p{L}\p{N}_:.-]{0,79}$/u.test(value)?value:'[动态频道已隐藏]';
  return {
    rows,
    clear(){rows.length=0;},
    start(channel,mode='runtime') {
      const row={id:++sequence,channel:safeChannel(channel),mode,started:clock(),elapsed:0,state:'pending'};
      rows.push(row);if(rows.length>limit)rows.splice(0,rows.length-limit);return row;
    },
    finish(row,state){row.state=state;row.elapsed=Math.max(0,clock()-row.started);},
  };
}
export function wrapInvoke(original,trace,isPreview=()=>false) {
  return function(...args) {
    const row=trace.start(args[0],isPreview()?'preview':'runtime');
    let result;
    try {result=Reflect.apply(original,this,args);}
    catch(error){trace.finish(row,'threw');throw error;}
    // Preserve arguments, receiver, fulfillment value and rejection reason.
    // Do not stringify payloads/results or read their properties for diagnostics.
    if(!(result instanceof Promise)) {trace.finish(row,'unverified');return result;}
    return result.then(value=>{trace.finish(row,'response');return value;},error=>{trace.finish(row,'rejected');throw error;});
  };
}
