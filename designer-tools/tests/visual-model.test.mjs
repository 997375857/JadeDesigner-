import test from 'node:test';
import assert from 'node:assert/strict';
import {parse} from 'parse5';
import {atPath,resolveElement,propertyEdits,insertionEdit,encodeEdits} from '../visual-model.js';

const path=tag=>[{tag:'html',index:0},{tag:'body',index:0},{tag,index:0}];
function descriptor(source,p) {
  const n=atPath(parse(source),p);
  return {path:p,id:n.attrs.find(a=>a.name==='id')?.value||'',attrs:n.attrs.map(a=>[a.name,a.value]).sort(([a],[b])=>a.localeCompare(b)),texts:(n.childNodes||[]).filter(n=>n.nodeName==='#text'&&n.value.trim()).map(n=>n.value)};
}
test('visual patches preserve identity, entities and untouched markup',()=>{
  const source='\uFEFF<!doctype html><html><head></head><body><button id="go" data-jade-handler="原回调" onclick="work()"><i></i> 标题 &amp; 内容 </button></body></html>',d=descriptor(source,path('button'));
  const edits=propertyEdits(source,d,[{kind:'style',key:'width',value:'140px'},{kind:'text',value:'新标题'}]);
  assert.equal(edits.length,2);assert.equal(edits[0].old,'<button id="go" data-jade-handler="原回调" onclick="work()">');
  assert.equal(edits[1].old,' 标题 &amp; 内容 ');assert.equal(edits[1].value,' 新标题 ');
  assert.deepEqual(encodeEdits(edits).split('\n').map(line=>line.split('\t').map(decodeURIComponent)),edits.map(e=>[e.kind,String(e.start),String(e.end),e.old,e.key,e.value]));
});
test('dynamic attributes, duplicate ids and indistinguishable controls are refused',()=>{
  const source='<html><body><button id="go">按钮</button></body></html>',d=descriptor(source,path('button'));
  assert.throws(()=>resolveElement(source,{...d,attrs:[['id','go'],['disabled','']]}),/不一致/);
  assert.throws(()=>resolveElement(source.replace('</body>','<div id="go"></div></body>'),d),/无法区分/);
  const anonymous='<html><body><span>相同</span><span>相同</span></body></html>';
  assert.throws(()=>resolveElement(anonymous,descriptor(anonymous,path('span'))),/无法区分/);
  assert.throws(()=>resolveElement('<html><body><button id="go" id="other">按钮</button></body></html>',d),/重复属性/);
});
test('palette needs explicit container and avoids both DOM and source identities',()=>{
  const source='<html><body><div id="area"></div><button id="jade_button_1" data-jade-handler="界面按钮2_被单击">已有</button></body></html>';
  const d=descriptor(source,path('div'));
  const insertion=insertionEdit(source,d,'button',['jade_button_3']);
  assert.equal(insertion.value,'4');assert.equal(insertion.old,'</div>');
  assert.throws(()=>insertionEdit(source,descriptor(source,path('button')),'button'),/布局容器/);
  const implicit='<p>missing explicit body';
  assert.throws(()=>insertionEdit(implicit,descriptor(implicit,path('body').slice(0,2)),'button'),/可靠|布局容器/);
});
test('selected option edits use independent exact source ranges',()=>{
  const source='<html><body><select id="choice"><option value="a">甲</option><option value="b">乙</option></select></body></html>';
  const edits=propertyEdits(source,descriptor(source,path('select')),[{kind:'select',value:'1',count:2}]);
  assert.deepEqual(edits.map(e=>[e.old,e.value]),[['<option value="b">','1']]);
});

test('anonymous containers resolve within a unique region and validate descendant identities',()=>{
  const source='<html><body><section id="a"><div class="grid"><button id="one">甲</button></div><div class="grid"><button id="two">乙</button></div></section><section id="b"><div class="grid"><button id="three">丙</button></div></section></body></html>';
  const region=path('section'),target=[...region,{tag:'div',index:0}];
  const d={...descriptor(source,target),ancestors:[{path:region,id:'a'}],ids:['one']};
  const edit=insertionEdit(source,d,'button');
  assert.equal(edit.start,source.indexOf('</div>'));
  const simple='<html><body><section id="a"><div class="grid"></div></section><section id="b"><div class="grid"></div></section></body></html>';
  assert.doesNotThrow(()=>resolveElement(simple,{...descriptor(simple,target),ancestors:d.ancestors,ids:[]}));
  assert.throws(()=>resolveElement(source,{...d,ids:['dynamic']}),/内部控件/);
  assert.throws(()=>resolveElement(source.replace('id="b"','id="a"'),d),/所属区域/);
  const identical='<html><body><section id="a"><div></div><div></div></section></body></html>';
  assert.throws(()=>resolveElement(identical,{...descriptor(identical,target),ancestors:d.ancestors,ids:[]}),/无法区分/);
});
