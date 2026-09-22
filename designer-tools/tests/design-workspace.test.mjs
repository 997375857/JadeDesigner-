import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdir} from 'node:fs/promises';
import {build} from 'esbuild';
import {chromium} from 'playwright';
import {resizeGeometry,RESIZE_DIRECTIONS} from '../design-components.js';

test('navigation rails render icons and export editable menu configuration',async()=>{
  const bundle=await build({entryPoints:['design-workspace.js'],bundle:true,format:'iife',write:false,loader:{'.md':'text','.css':'text'}});
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage({viewport:{width:1440,height:960}}),errors=[];
    page.on('pageerror',e=>errors.push(e.message));
    await page.route('https://jade-design.test/**',r=>r.fulfill({contentType:'text/html',body:'<!doctype html><html><head></head><body></body></html>'}));
    await page.goto('https://jade-design.test/');await page.addScriptTag({content:bundle.outputFiles[0].text});
    await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('就绪'));
    await page.getByLabel('搜索组件').fill('导航');
    await page.getByRole('button',{name:'窄侧导航',exact:true}).click();
    const rail=page.locator('#canvas .preset-navigation-rail');
    assert.equal(await rail.locator('.nav-icon svg').count(),4);
    assert.equal(await rail.locator('.nav-top svg').count(),1);
    assert.equal(await rail.locator('.nav-item').first().evaluate(el=>getComputedStyle(el).flexDirection),'column');
    await page.getByLabel('第 1 项文字',{exact:true}).fill('工作台');await page.getByLabel('第 1 项文字',{exact:true}).press('Tab');
    await page.getByLabel('第 1 项图标',{exact:true}).selectOption('chart');
    assert.equal(await rail.locator('[data-lucide="chart-column"]').count(),1);
    await page.getByLabel('选中第 2 项',{exact:true}).check();
    assert.equal(await rail.locator('.nav-item.active').getAttribute('data-item-id'),'item-2');
    await page.getByRole('button',{name:'添加导航项',exact:true}).click();
    assert.equal(await rail.locator('.nav-item').count(),5);
    await page.getByRole('button',{name:'删除导航第 2 项',exact:true}).click();
    assert.equal(await rail.locator('.nav-item.active').getAttribute('data-item-id'),'item-1');
    await page.getByRole('button',{name:'生成 AI 开发任务',exact:true}).click();
    const task=await page.getByLabel('AI 开发任务',{exact:true}).inputValue();
    assert.match(task,/"icon": "chart"/);assert.match(task,/工作台/);
    await page.getByRole('button',{name:'关闭',exact:true}).click();
    await page.screenshot({path:'../artifacts/design-workspace/navigation-rail.png',fullPage:true});
    await page.getByRole('button',{name:'侧边导航',exact:true}).click();
    assert.equal(await page.locator('#canvas .preset-sidebar .nav-icon svg').count(),4);
    assert.equal(await page.locator('#canvas .preset-sidebar .nav-item').first().evaluate(el=>getComputedStyle(el).flexDirection),'row');
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});

test('all eight resize edges work at reduced zoom and presets remain editable',async()=>{
  const bundle=await build({entryPoints:['design-workspace.js'],bundle:true,format:'iife',write:false,loader:{'.md':'text','.css':'text'}});
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage({viewport:{width:1440,height:960}});
    await page.route('https://jade-design.test/**',r=>r.fulfill({contentType:'text/html',body:'<!doctype html><html><head></head><body></body></html>'}));
    await page.goto('https://jade-design.test/');await page.addScriptTag({content:bundle.outputFiles[0].text});
    await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('就绪'));
    assert.equal(await page.locator('#palette .component').count(),32);
    await page.getByRole('button',{name:'按钮',exact:true}).click();
    for(const [label,value] of [['左边','160'],['顶边','160'],['高度','100']]){const input=page.getByLabel(label,{exact:true});await input.fill(value);await input.press('Tab');}
    await page.getByRole('button',{name:'缩小',exact:true}).click();
    const node=page.locator('[data-node-id="button-1"]');
    assert.equal(await node.locator('.resize-handle').count(),8);
    const zoom=(await node.boundingBox()).width/160,delta=Math.round(12/zoom/4)*4;
    for(const direction of Object.keys(RESIZE_DIRECTIONS)){
      const handle=node.locator(`[data-direction="${direction}"]`),box=await handle.boundingBox();
      const cursor=await handle.evaluate(el=>getComputedStyle(el).cursor);
      assert.equal(cursor,['n','s'].includes(direction)?'ns-resize':['e','w'].includes(direction)?'ew-resize':['nw','se'].includes(direction)?'nwse-resize':'nesw-resize');
      await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();
      await page.mouse.move(box.x+box.width/2+12,box.y+box.height/2+12,{steps:4});await page.mouse.up();
      const expected=resizeGeometry({x:160,y:160,width:160,height:100},direction,delta,delta,{width:1100,height:760});
      for(const [key,label] of [['x','左边'],['y','顶边'],['width','宽度'],['height','高度']])assert.equal(Number(await page.getByLabel(label,{exact:true}).inputValue()),expected[key],direction+' '+key);
      await page.getByRole('button',{name:'撤销',exact:true}).click();
    }
    const search=page.getByLabel('搜索组件');await search.fill('开关');
    await page.getByRole('button',{name:'开关',exact:true}).click();
    assert.equal(await page.locator('#canvas .node-view.preset-switch').count(),1);
    await page.getByLabel('显示文字',{exact:true}).fill('启用提醒');await page.getByLabel('显示文字',{exact:true}).press('Tab');
    assert.match(await page.locator('#canvas').innerText(),/启用提醒/);
    await search.fill('');await page.screenshot({path:'../artifacts/design-workspace/resize-components.png',fullPage:true});
  }finally{await browser.close();}
});

test('standalone saved designs and unsaved drafts survive reopening',async()=>{
  const result=await build({entryPoints:['design-workspace.js'],bundle:true,format:'iife',write:false,loader:{'.md':'text','.css':'text'}});
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage(),errors=[];
    page.on('pageerror',e=>errors.push(e.message));
    await page.route('https://jade-design.test/**',route=>route.fulfill({contentType:'text/html',body:'<!doctype html><html><head></head><body></body></html>'}));
    async function open(){
      await page.goto('https://jade-design.test/');
      await page.addScriptTag({content:result.outputFiles[0].text});
      await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('就绪')||document.querySelector('#status').textContent.includes('已恢复'));
    }
    await open();
    await page.getByRole('button',{name:'按钮',exact:true}).click();
    const downloaded=page.waitForEvent('download');
    await page.keyboard.press('Control+s');
    await downloaded;
    await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('已本地保存'));
    await open();
    assert.equal(await page.locator('#canvas .node').count(),1);
    await page.getByRole('button',{name:'编辑框',exact:true}).click();
    page.once('dialog',dialog=>dialog.accept());
    await open();
    assert.equal(await page.locator('#canvas .node').count(),2);
    assert.match(await page.locator('#status').textContent(),/待保存/);
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});

test('design workspace edits, drags, saves and exports full contract without invoking business',async()=>{
  const result=await build({entryPoints:['design-workspace.js'],bundle:true,format:'iife',write:false,loader:{'.md':'text','.css':'text'}});
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage({viewport:{width:1280,height:820}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
    // Match WebView2 NavigateToString: opaque about:blank origin, no storage.
    await page.setContent('<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src \'none\'; script-src \'unsafe-inline\'; style-src \'unsafe-inline\'; img-src data: blob:;"></head><body></body></html>');
    await page.evaluate(()=>{
      window.sent=[];window.__jadeDesignToken='test';let callback;
      window.chrome={webview:{addEventListener:(_,fn)=>callback=fn,postMessage:wire=>{
        if(wire.startsWith('JADE_DESIGN_DRAFT\t')){const text=decodeURIComponent(wire.split('\t')[2]);window.lastDraft=text?JSON.parse(text):null;return;}
        const [,id,action,...data]=wire.split('\t').map(decodeURIComponent);window.sent.push({action,data});
        const fields=action==='design_load'?['','<button id="existing">已有按钮</button>','E:\\test.e']:['已保存'];
        queueMicrotask(()=>callback({data:['JADE_TOOL_RESULT',id,'1',...fields.map(encodeURIComponent)].join('\t')}));
      }}};
    });
    await page.addScriptTag({content:result.outputFiles[0].text});
    await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('就绪'));
    await page.getByRole('button',{name:'超级列表框',exact:true}).click();
    await page.getByLabel('第 1 列标题',{exact:true}).fill('用户');await page.getByLabel('第 1 列标题',{exact:true}).press('Tab');
    await page.getByLabel('固定行高').fill('40');await page.getByLabel('固定行高').press('Tab');
    await page.getByRole('button',{name:'添加列',exact:true}).click();
    await page.getByRole('button',{name:'按钮',exact:true}).click();
    const node=page.locator('[data-node-id="button-1"]');const box=await node.boundingBox();
    await page.mouse.move(box.x+box.width/2,box.y+box.height/2);await page.mouse.down();await page.mouse.move(box.x+box.width/2+60,box.y+box.height/2+60,{steps:5});await page.mouse.up();
    assert.ok(Number(await page.getByLabel('左边',{exact:true}).inputValue())>24);
    await page.getByRole('button',{name:'AI 对接',exact:true}).click();
    await page.getByRole('button',{name:'仅导入控件身份'}).click();
    assert.equal(await page.evaluate(()=>window.lastDraft.nodes.length),3);
    await page.getByRole('button',{name:'组件',exact:true}).click();
    await page.getByRole('button',{name:'生成 AI 开发任务'}).click();
    await page.waitForFunction(()=>window.sent.some(m=>m.action==='design_export'));
    const messages=await page.evaluate(()=>window.sent),saved=JSON.parse(messages.find(m=>m.action==='design_save').data[1]);
    assert.equal(saved.nodes[0].columns[0].title,'用户');assert.equal(saved.nodes[0].rowHeight,40);assert.equal(saved.nodes[0].columns.length,4);
    assert.ok(messages.find(m=>m.action==='design_export').data[1].includes('# AI 生成 UI 与 JadeView 易语言支持库对接规范'));
    assert.equal(await page.evaluate(()=>window.lastDraft),null);
    await page.getByRole('button',{name:'关闭',exact:true}).click();
    await mkdir('../artifacts/design-workspace',{recursive:true});
    await page.screenshot({path:'../artifacts/design-workspace/desktop.png',fullPage:true});
    await page.setViewportSize({width:760,height:700});await page.screenshot({path:'../artifacts/design-workspace/narrow.png',fullPage:true});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
    await page.setViewportSize({width:390,height:844});await page.screenshot({path:'../artifacts/design-workspace/mobile.png',fullPage:true});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});
test('themes, layers, grouping, zoom and resizing survive export and undo',async()=>{
  const result=await build({entryPoints:['design-workspace.js'],bundle:true,format:'iife',write:false,loader:{'.md':'text','.css':'text'}});
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage({viewport:{width:1440,height:960}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.setContent('<html><head></head><body></body></html>');
    await page.evaluate(()=>{window.__jadeDesignToken='test';window.sent=[];let callback;window.chrome={webview:{addEventListener:(_,fn)=>callback=fn,postMessage:wire=>{
      if(!wire.startsWith('JADE_TOOL\t'))return;const [,id,action,...data]=wire.split('\t').map(decodeURIComponent);window.sent.push({action,data});
      const fields=action==='design_load'?['','','E:\\theme-test.e','']:['ok'];queueMicrotask(()=>callback({data:['JADE_TOOL_RESULT',id,'1',...fields.map(encodeURIComponent)].join('\t')}));
    }}};});
    await page.addScriptTag({content:result.outputFiles[0].text});await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('就绪'));
    await page.getByRole('button',{name:'按钮',exact:true}).click();await page.getByRole('button',{name:'按钮',exact:true}).click();
    const editorColor=await page.locator('#tools .primary-command').evaluate(el=>getComputedStyle(el).backgroundColor);
    await page.getByRole('button',{name:'配色',exact:true}).click();await page.getByRole('button',{name:'玫红',exact:true}).click();
    assert.equal(await page.locator('#tools .primary-command').evaluate(el=>getComputedStyle(el).backgroundColor),editorColor);
    await page.getByRole('button',{name:'形状',exact:true}).click();await page.getByRole('button',{name:'柔和',exact:true}).click();
    await page.waitForFunction(()=>getComputedStyle(document.querySelector('[data-node-id="button-2"] .node-view')).borderRadius==='24px');
    await page.getByRole('button',{name:'字体',exact:true}).click();await page.getByLabel('全局字号',{exact:true}).fill('18');await page.getByLabel('全局字号',{exact:true}).press('Tab');
    await page.getByRole('button',{name:'图层',exact:true}).click();
    await page.getByRole('button',{name:'锁定 button-2',exact:true}).click();
    const before=await page.locator('[data-node-id="button-2"]').getAttribute('style'),box=await page.locator('[data-node-id="button-2"]').boundingBox();
    await page.mouse.move(box.x+15,box.y+12);await page.mouse.down();await page.mouse.move(box.x+60,box.y+50);await page.mouse.up();
    assert.equal(await page.locator('[data-node-id="button-2"]').getAttribute('style'),before);
    await page.getByRole('button',{name:'解锁 button-2',exact:true}).click();
    await page.getByRole('button',{name:'隐藏 button-1',exact:true}).click();assert.equal(await page.locator('[data-node-id="button-1"]').count(),0);
    await page.getByRole('button',{name:'显示 button-1',exact:true}).click();
    await page.getByRole('button',{name:'按钮 · button-1',exact:true}).click();await page.getByRole('button',{name:'按钮 · button-2',exact:true}).click({modifiers:['Shift']});
    await page.getByRole('button',{name:'组合',exact:true}).click();
    await page.locator('#layers').getByRole('button',{name:'复制控件',exact:true}).count().then(count=>assert.equal(count,0));
    await page.locator('.layer-actions').getByRole('button',{name:'复制控件',exact:true}).click();
    assert.equal(await page.locator('#canvas .node').count(),4);
    await page.getByRole('button',{name:'取消组合',exact:true}).click();
    await page.getByRole('button',{name:'按钮 · button-4',exact:true}).click();
    await page.getByRole('button',{name:'下一层',exact:true}).click();
    assert.deepEqual(await page.locator('#canvas .node').evaluateAll(nodes=>nodes.map(n=>n.dataset.nodeId)),['button-1','button-2','button-4','button-3']);
    await page.getByRole('button',{name:'实际尺寸',exact:true}).click();
    const handle=page.locator('[data-node-id="button-4"] .resize-handle[data-direction="se"]');const h=await handle.boundingBox();
    await page.mouse.move(h.x+4,h.y+4);await page.mouse.down();await page.mouse.move(h.x+44,h.y+24,{steps:3});await page.mouse.up();
    assert.equal(Number(await page.getByLabel('宽度',{exact:true}).inputValue()),200);
    await page.getByRole('button',{name:'撤销',exact:true}).click();assert.equal(Number(await page.getByLabel('宽度',{exact:true}).inputValue()),160);
    await page.getByRole('button',{name:'生成 AI 开发任务',exact:true}).click();await page.waitForFunction(()=>window.sent.some(m=>m.action==='design_export'));
    const saved=await page.evaluate(()=>JSON.parse(window.sent.findLast(m=>m.action==='design_save').data[1]));
    assert.equal(saved.theme.primary,'#bd4266');assert.equal(saved.theme.radius,24);assert.equal(saved.theme.fontSize,18);assert.equal(saved.nodes.length,4);
    assert.ok(saved.nodes.find(n=>n.id==='button-1').style.group);assert.equal(saved.nodes.find(n=>n.id==='button-4').style.group,'');
    await page.getByRole('button',{name:'关闭',exact:true}).click();await page.getByRole('button',{name:'适应窗口',exact:true}).click();
    await page.screenshot({path:'../artifacts/design-workspace/layers-theme.png',fullPage:true});assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});
