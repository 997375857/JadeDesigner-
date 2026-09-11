import test from 'node:test';
import assert from 'node:assert/strict';
import {createTrace,wrapInvoke} from '../diagnostics-core.js';

test('runtime tap preserves receiver, arguments and result without retaining payload',async()=>{
  let now=0;const trace=createTrace(2,()=>now);
  const receiver={},payload={secret:'do-not-log'},options={timeout:17},response={ok:false};
  const original=function(...args){assert.equal(this,receiver);assert.deepEqual(args,['app:test',payload,options]);return Promise.resolve(response);};
  const result=wrapInvoke(original,trace).call(receiver,'app:test',payload,options);now=25;
  assert.equal(await result,response);
  assert.equal(trace.rows[0].state,'response');assert.equal(trace.rows[0].elapsed,25);
  assert.ok(!JSON.stringify(trace.rows).includes('do-not-log'));assert.ok(!JSON.stringify(trace.rows).includes('ok'));
});
test('rejection and synchronous errors retain identity and do not retry',async()=>{
  const trace=createTrace();const error=new Error('secret');let calls=0;
  await assert.rejects(wrapInvoke(()=>{++calls;return Promise.reject(error);},trace)('app:x'),e=>e===error);
  assert.equal(calls,1);assert.equal(trace.rows[0].state,'rejected');
  assert.throws(()=>wrapInvoke(()=>{throw error;},trace)('app:x'),e=>e===error);
  assert.equal(trace.rows[1].state,'threw');assert.ok(!JSON.stringify(trace.rows).includes('secret'));
});
test('preview is distinct, records bounded, unsafe channel suppressed',async()=>{
  const trace=createTrace(2);
  const invoke=wrapInvoke(()=>Promise.resolve('ok'),trace,()=>true);
  for(const name of ['app:one','app:two','https://host/?token=secret'])await invoke(name);
  assert.equal(trace.rows.length,2);assert.equal(trace.rows[1].mode,'preview');
  assert.equal(trace.rows[1].channel,'[动态频道已隐藏]');assert.ok(!JSON.stringify(trace.rows).includes('secret'));
  trace.clear();assert.equal(trace.rows.length,0);
});
test('pending and non-Promise results are not reported as IPC success',()=>{
  const trace=createTrace();const value={then:'not-a-promise'};
  assert.equal(wrapInvoke(()=>value,trace)('app:test'),value);assert.equal(trace.rows[0].state,'unverified');
  wrapInvoke(()=>new Promise(()=>{}),trace)('app:pending');assert.equal(trace.rows[1].state,'pending');
});
