import test from 'node:test';
import assert from 'node:assert/strict';
import {newDesign,newNode,validate,importControls,buildRequest,taskChanges,sourceIds,themeOf,styleOf,moveLayer,duplicateNodes} from '../design-model.js';
import {newComponent} from '../design-model.js';
import {PRESETS,COMPONENT_GROUPS,resizeGeometry} from '../design-components.js';
test('component presets preserve supported types, styling, stable IDs and export semantics',()=>{
  const doc=newDesign();
  for(const key of COMPONENT_GROUPS.flatMap(([,keys])=>keys))doc.nodes.push(newComponent(key,doc.nodes));
  assert.equal(doc.nodes.length,32);validate(doc);
  assert.equal(new Set(doc.nodes.map(n=>n.id)).size,32);
  for(const [preset,p] of Object.entries(PRESETS))assert.equal(doc.nodes.find(n=>n.preset===preset).kind,p.kind);
  const task=buildRequest(doc);assert.match(task,/"preset":"password"/);assert.match(task,/"preset":"dialog"/);
  assert.doesNotMatch(task,/SPEC|完整对接规范（构建时/);
  const copy=structuredClone(doc);copy.nodes[0].preset='dialog';assert.throws(()=>validate(copy),/不匹配/);
});
test('navigation presets validate items and preserve independent configuration on duplication',()=>{
  const doc=newDesign();doc.nodes.push(newComponent('navigation-rail',[]));
  const nav=doc.nodes[0].navigation;assert.equal(nav.items.length,4);assert.equal(nav.items[0].icon,'house');
  duplicateNodes(doc,[doc.nodes[0].id]);doc.nodes[1].navigation.items[0].text='工作台';
  assert.equal(nav.items[0].text,'首页');validate(doc);
  assert.match(buildRequest(doc),/"preset":"navigation-rail"/);
  for(const alter of [n=>n.navigation=null,n=>n.navigation.items=[],n=>n.navigation.selectedId='missing',n=>n.navigation.items[0].icon='missing',n=>n.navigation.items.push({...n.navigation.items[0]})]){
    const copy=structuredClone(doc);alter(copy.nodes[0]);assert.throws(()=>validate(copy),/导航/);
  }
});
test('eight-way resizing anchors opposite edges and clamps dimensions and canvas bounds',()=>{
  const n={x:100,y:100,width:160,height:80},bounds={width:500,height:400};
  const expected={nw:[120,112,140,68],n:[100,112,160,68],ne:[100,112,180,68],e:[100,100,180,80],se:[100,100,180,92],s:[100,100,160,92],sw:[120,100,140,92],w:[120,100,140,80]};
  for(const [direction,values] of Object.entries(expected))assert.deepEqual(Object.values(resizeGeometry(n,direction,20,12,bounds)),values);
  assert.deepEqual(resizeGeometry(n,'nw',-1000,-1000,bounds),{x:0,y:0,width:260,height:180});
  assert.deepEqual(resizeGeometry(n,'nw',1000,1000,bounds),{x:236,y:156,width:24,height:24});
  assert.deepEqual(resizeGeometry(n,'se',1000,1000,bounds),{x:100,y:100,width:400,height:300});
  assert.deepEqual(resizeGeometry(n,'se',-1000,-1000,bounds),{x:100,y:100,width:24,height:24});
});
test('design IDs stay unique and grids require named columns and fixed height',()=>{
  const d=newDesign();d.nodes.push(newNode('super-list',d.nodes));d.nodes.push(newNode('super-list',d.nodes));
  assert.equal(d.nodes[1].id,'super-list-2');assert.equal(validate(d),d);
  d.nodes[0].columns=[];assert.throws(()=>validate(d),/至少需要一列/);
});
test('schema rejects invalid types, duplicate IDs, malicious geometry and unsupported versions',()=>{
  for(const alter of [d=>d.version=2,d=>d.width=NaN,d=>d.nodes.push({}),d=>{d.nodes.push(newNode('edit',[]));d.nodes.push(newNode('edit',[]));},d=>{d.nodes.push(newNode('edit',[]));d.nodes[0].x=-1;}]){
    const d=newDesign();alter(d);assert.throws(()=>validate(d));
  }
});
test('HTML import preserves stable metadata, is idempotent and never executes scripts',()=>{
  const html='<button id="添加" data-jade-handler="添加被单击" data-jade-channel="app:add">添加</button><div id="list" data-jade-id="list" data-jade-control="super-list"></div><script>throw Error("no");</script>';
  const d=importControls(html,newDesign());assert.equal(d.nodes.length,2);assert.equal(d.baseline.length,2);
  assert.equal(d.nodes[0].channel,'app:add');assert.equal(importControls(html,d).nodes.length,2);
  assert.throws(()=>importControls('<div id="a" data-jade-id="b" data-jade-control="edit"></div>',d),/不一致/);
  assert.throws(()=>importControls('<button id="same"></button><button id="same"></button>',d),/重复 ID/);
  assert.throws(()=>newNode('constructor',[]),/不支持/);
  assert.equal(newNode('button',sourceIds('<div id="button-1"></div>')).id,'button-2');
});
test('AI task reports only additions, changes and removals against imported controls',()=>{
  const d=importControls('<button id="old">原按钮</button>',newDesign());d.nodes=[];d.nodes.push(newNode('super-list',[]));
  const task=buildRequest(d);assert.match(task,/新增：.*"kind":"super-list"/);assert.match(task,/删除：.*"id":"old"/);
  assert.match(task,/beginBatch\/endBatch/);assert.doesNotMatch(task,/## 设计稿|```css|完整对接规范（构建时/);
  assert.ok(task.length<3000);
});

test('text-only changes stay short; untouched controls and design-only flags stay out',()=>{
  const d=newDesign(),original=newNode('label',[]),untouched=newNode('button',[original]);
  Object.assign(original,{id:'jade-text-1',source:true,sourceLocator:{selector:'body > span:nth-of-type(1)',text:'百度弹窗助手'},text:'百度弹窗助手'});
  d.baseline=[structuredClone(original),structuredClone(untouched)];d.nodes=structuredClone(d.baseline);
  d.nodes[0].text='新的标题';d.nodes[0].style={bold:true,hidden:true,locked:true};
  const task=buildRequest(d),changes=taskChanges(d);
  assert.equal(changes.changed.length,1);assert.deepEqual(changes.changed[0].fields,{text:{from:'百度弹窗助手',to:'新的标题'},style:{bold:true}});
  assert.match(task,/"selector":"body > span:nth-of-type\(1\)"/);assert.match(task,/"originalText":"百度弹窗助手"/);
  assert.doesNotMatch(task,/"button-1"|"hidden"|"locked"|"sourcePage"|"baseline"/);
  assert.ok(task.length<1100);
  d.nodes=structuredClone(d.baseline);d.nodes[0].style={hidden:true,locked:true};
  assert.match(buildRequest(d),/没有待实现的变更/);
});

test('display text is not mistaken for an existing HTML identity',()=>{
  const d=newDesign(),duplicate=newNode('container',[]),newText=newNode('label',[duplicate]);
  duplicate.text='logOutput';newText.text='新说明';d.nodes=[duplicate,newText];
  const html='<div id="logOutput"></div>';
  const changes=taskChanges(d,html),task=buildRequest(d,html);
  assert.equal(changes.added.length,2);assert.equal(changes.review.length,0);
  assert.match(task,/新增：.*"id":"container-1"/);
  assert.match(task,/"newId":"container-1","text":"logOutput","existingId":"logOutput"/);
  assert.doesNotMatch(task,/待核对/);
  duplicate.id='logOutput';assert.equal(taskChanges(d,html).review[0].existingId,'logOutput');
});
test('new multiline edit is distinct from a same-named existing log output',()=>{
  const d=newDesign(),input=newComponent('textarea',[]);
  input.id='signInput';input.text='logOutput';d.nodes=[input];
  const task=buildRequest(d,'<div id="logOutput" class="log-output"></div>');
  assert.match(task,/"id":"signInput","kind":"edit","text":"logOutput"/);
  assert.match(task,/"preset":"textarea"/);
  assert.match(task,/"newId":"signInput","text":"logOutput","existingId":"logOutput"/);
  assert.match(task,/可输入的多行编辑框/);
  assert.doesNotMatch(task,/待核对/);
});
test('old drafts inherit theme; unsafe styling is rejected; duplication preserves groups with new IDs',()=>{
  const d=newDesign();delete d.theme;assert.equal(themeOf(validate(d)).radius,6);
  d.nodes.push(newNode('button',[]));d.nodes.push(newNode('button',d.nodes));
  d.nodes.forEach(n=>n.style={group:'old-group',locked:true});
  const ids=duplicateNodes(d,['button-1','button-2']);assert.deepEqual(ids,['button-3','button-4']);
  assert.equal(styleOf(d.nodes[2]).locked,false);assert.equal(d.nodes[2].style.group,d.nodes[3].style.group);assert.notEqual(d.nodes[2].style.group,'old-group');
  moveLayer(d,'button-1',3);assert.equal(d.nodes.at(-1).id,'button-1');validate(d);
  for(const value of [{primary:'url(https://invalid)'},{radius:-1},{font:'unknown'},[]]){const copy=structuredClone(d);copy.theme=value;assert.throws(()=>validate(copy));}
});

test('static text locators survive drafts but never belong to new duplicated controls',()=>{
  const d=newDesign(),n=newNode('label',[]);
  Object.assign(n,{source:true,sourceLocator:{selector:'body > span:nth-of-type(1)',text:'原文字'}});
  n.style={bold:true};d.nodes=[n];d.baseline=[structuredClone(n)];validate(d);
  const [id]=duplicateNodes(d,[n.id]);const copy=d.nodes.find(n=>n.id===id);
  assert.equal(copy.sourceLocator,undefined);assert.equal(copy.source,undefined);assert.equal(copy.style.bold,true);
  for(const value of [null,{},'selector',{selector:'body',text:'x'},{selector:'body > span',text:7}]){
    const bad=structuredClone(d);bad.nodes[0].sourceLocator=value;assert.throws(()=>validate(bad),/定位/);
  }
  const bad=structuredClone(d);bad.nodes[0].style.bold='yes';assert.throws(()=>validate(bad),/样式/);
});
