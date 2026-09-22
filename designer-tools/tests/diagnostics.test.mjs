import test from 'node:test';
import assert from 'node:assert/strict';
import {createTrace,wrapInvoke} from '../diagnostics-core.js';
import {createNativeDiagnostics} from '../native-diagnostics.js';

test('native dock receives matching request and response with lossless wire fields',async()=>{
  const messages=[];
  const native=createNativeDiagnostics(wire=>messages.push(wire),()=>({handler:'按钮被单击',assembly:'用户程序集'}),()=>true);
  const payload={text:'中文\t%\n',long:'x'.repeat(40000)},response={ok:true};
  const result=await native.wrap(()=>Promise.resolve(response))('app:test',payload);
  const rows=messages.map(wire=>wire.split('\t').slice(1).map(decodeURIComponent));
  assert.equal(result,response);assert.equal(rows.length,2);assert.equal(rows[0][0],rows[1][0]);
  assert.equal(rows[0][1],'按钮被单击');assert.deepEqual(JSON.parse(rows[0][3]),payload);
  assert.deepEqual(JSON.parse(rows[1][4]),response);assert.match(rows[1][5],/预览模拟（未调用易语言）/);
  assert.equal(rows[1][6],'用户程序集');
});
test('native diagnostic transport failure cannot break business calls',async()=>{
  const native=createNativeDiagnostics(()=>{throw new Error('dock closed');},()=>({}),()=>false);
  assert.equal(await native.wrap(()=>Promise.resolve('ok'))('app:test','{}'),'ok');
});

test('runtime tap preserves receiver, arguments, payload and result',async()=>{
  let now=0;const trace=createTrace(2,()=>now);
  const receiver={},payload={secret:'do-not-log'},options={timeout:17},response={ok:false};
  const original=function(...args){assert.equal(this,receiver);assert.deepEqual(args,['app:test',payload,options]);return Promise.resolve(response);};
  const result=wrapInvoke(original,trace).call(receiver,'app:test',payload,options);now=25;
  assert.equal(await result,response);
  assert.equal(trace.rows[0].state,'response');assert.equal(trace.rows[0].elapsed,25);
  assert.equal(trace.rows[0].payload,JSON.stringify(payload));assert.equal(trace.rows[0].response,JSON.stringify(response));
});
test('rejection and synchronous errors retain identity and do not retry',async()=>{
  const trace=createTrace();const error=new Error('secret');let calls=0;
  await assert.rejects(wrapInvoke(()=>{++calls;return Promise.reject(error);},trace)('app:x'),e=>e===error);
  assert.equal(calls,1);assert.equal(trace.rows[0].state,'rejected');
  assert.throws(()=>wrapInvoke(()=>{throw error;},trace)('app:x'),e=>e===error);
  assert.equal(trace.rows[1].state,'threw');assert.equal(trace.rows[0].error,'secret');assert.equal(trace.rows[1].error,'secret');
});
test('preview is distinct, records bounded, unsafe channel suppressed',async()=>{
  const trace=createTrace(2);
  const invoke=wrapInvoke(()=>Promise.resolve('ok'),trace,()=>true);
  for(const name of ['app:one','app:two','https://host/?token=secret'])await invoke(name);
  assert.equal(trace.rows.length,2);assert.equal(trace.rows[1].mode,'preview');
  assert.equal(trace.rows[1].channel,'https://host/?token=secret');
  trace.clear();assert.equal(trace.rows.length,0);
});
test('pending and non-Promise results are not reported as IPC success',()=>{
  const trace=createTrace();const value={then:'not-a-promise'};
  assert.equal(wrapInvoke(()=>value,trace)('app:test'),value);assert.equal(trace.rows[0].state,'unverified');
  wrapInvoke(()=>new Promise(()=>{}),trace)('app:pending');assert.equal(trace.rows[1].state,'pending');
});
