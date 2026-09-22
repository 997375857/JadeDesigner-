import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {newDesign,newNode,validate,importControls,buildRequest,sourceIds,themeOf,styleOf,moveLayer,duplicateNodes} from '../design-model.js';
import {newComponent} from '../design-model.js';
import {PRESETS,COMPONENT_GROUPS,resizeGeometry} from '../design-components.js';
test('component presets preserve supported types, styling, stable IDs and export semantics',()=>{
  const doc=newDesign();
  for(const key of COMPONENT_GROUPS.flatMap(([,keys])=>keys))doc.nodes.push(newComponent(key,doc.nodes));
  assert.equal(doc.nodes.length,32);validate(doc);
  assert.equal(new Set(doc.nodes.map(n=>n.id)).size,32);
  for(const [preset,p] of Object.entries(PRESETS))assert.equal(doc.nodes.find(n=>n.preset===preset).kind,p.kind);
  const task=buildRequest(doc,'SPEC');assert.match(task,/不.*伪造对应的模块类/);assert.match(task,/input\[type=password\]/);
  const copy=structuredClone(doc);copy.nodes[0].preset='dialog';assert.throws(()=>validate(copy),/不匹配/);
});
test('navigation presets validate items and preserve independent configuration on duplication',()=>{
  const doc=newDesign();doc.nodes.push(newComponent('navigation-rail',[]));
  const nav=doc.nodes[0].navigation;assert.equal(nav.items.length,4);assert.equal(nav.items[0].icon,'house');
  duplicateNodes(doc,[doc.nodes[0].id]);doc.nodes[1].navigation.items[0].text='工作台';
  assert.equal(nav.items[0].text,'首页');validate(doc);
  assert.match(buildRequest(doc,'SPEC'),/navigation-rail.*图标在上/);
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
test('AI task includes entire authoritative spec and explicit incremental intent',async()=>{
  const spec=await readFile(new URL('../../docs/AI生成UI与JadeView支持库对接规范.md',import.meta.url),'utf8');
  const d=importControls('<button id="old">原按钮</button>',newDesign());d.nodes=[];d.nodes.push(newNode('super-list',[]));
  const task=buildRequest(d,spec);assert.ok(task.includes(spec));assert.match(task,/"removed": \[\s+"old"/);assert.match(task,/beginBatch\/endBatch/);assert.match(task,/不直接修改 WPE/);
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
