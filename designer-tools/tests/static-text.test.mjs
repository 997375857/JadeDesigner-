import test from 'node:test';
import assert from 'node:assert/strict';
import {locateText} from '../static-text.js';
const descriptor=(text,tag='button',id='go')=>({path:[{tag:'html',index:0},{tag:'body',index:0},{tag,index:0}],id,text});
test('entities and icon preserve a precise text-only source range',()=>{
  const source='<button id="go"><i class="icon"></i> 导入 &amp; 测试</button>';
  const r=locateText(source,descriptor(' 导入 & 测试'));
  assert.equal(r.old,' 导入 &amp; 测试');
  assert.equal(source.slice(r.start,r.end),r.old);
});
test('UTF16 offsets preserve BOM and supplementary Unicode before a label',()=>{
  const source='\uFEFF<!-- 😀 --><button id="go">标题</button>';
  assert.equal(locateText(source,descriptor('标题')).start,source.indexOf('标题'));
});
test('dynamic text and source identity changes are refused',()=>{
  assert.throws(()=>locateText('<button id="go">原文</button>',descriptor('动态文案')));
  assert.throws(()=>locateText('<button id="other">原文</button>',descriptor('原文')));
});
test('mixed text and executable elements are refused',()=>{
  assert.throws(()=>locateText('<button id="go">前<i></i>后</button>',descriptor('前')));
  assert.throws(()=>locateText('<script id="go">work()</script>',descriptor('work()','script')));
});
test('indented text maps without changing surrounding tags',()=>{
  const source='<button id="go">\r\n  标题\r\n</button>';
  assert.equal(locateText(source,descriptor('\n  标题\n')).old,'\r\n  标题\r\n');
});
