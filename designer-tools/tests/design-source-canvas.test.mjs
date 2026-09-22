import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {build} from 'esbuild';
import {chromium} from 'playwright';
import {newDesign,newNode} from '../design-model.js';

test('original HTML and local CSS load inside the existing canvas with selection, editing, resize, undo and new components',async()=>{
  const bundle=await build({entryPoints:['design-workspace.js'],bundle:true,format:'iife',write:false,loader:{'.md':'text','.css':'text'}});
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage({viewport:{width:1700,height:1050}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.route('https://jade-design-assets.invalid/theme.css',r=>r.fulfill({contentType:'text/css',body:'body{margin:0;font:14px sans-serif;background:white}#panel{position:absolute;left:40px;top:64px;width:760px;height:440px;border:1px solid #ddd;border-radius:8px;padding:24px;box-sizing:border-box}#go{width:156px;height:40px;background:#27292d;color:white;border:0;border-radius:6px}'}));
    const html='<!doctype html><html><head><link rel="stylesheet" href="theme.css"></head><body><section id="panel"><button id="go" data-jade-handler="新建智能体被单击" onclick="parent.business=true"><i>+</i><span>新建智能体</span></button><p>原网页内容</p></section><script>parent.business=true</script></body></html>';
    await page.setContent('<!doctype html><html><head><meta http-equiv="Content-Security-Policy" content="default-src \'none\'; script-src \'unsafe-inline\'; style-src \'unsafe-inline\' https://jade-design-assets.invalid; img-src data: blob: https://jade-design-assets.invalid; font-src data: https://jade-design-assets.invalid; frame-src \'self\' about:;"></head><body></body></html>');
    await page.evaluate(html=>{
      window.business=false;window.sent=[];window.__jadeDesignToken='test';let receive;
      window.chrome={webview:{addEventListener:(_,fn)=>receive=fn,postMessage:wire=>{
        if(wire.startsWith('JADE_DESIGN_DRAFT\t')){window.draft=JSON.parse(decodeURIComponent(wire.split('\t')[2])||'null');return;}
        const [,id,action,...args]=wire.split('\t').map(decodeURIComponent);window.sent.push({action,args});
        const values=action==='design_load'?['',html,'E:\\canvas.e']:action==='design_edit_source'?[html,'https://jade-design-assets.invalid/']:['ok'];
        queueMicrotask(()=>receive({data:['JADE_TOOL_RESULT',id,'1',...values.map(encodeURIComponent)].join('\t')}));
      }}};
    },html);
    await page.addScriptTag({content:bundle.outputFiles[0].text});
    await page.waitForFunction(()=>document.querySelector('#status').textContent.includes('就绪'));
    const topUrl=page.url();
    await page.getByRole('button',{name:'编辑现有网页',exact:true}).click();
    await page.getByRole('status').filter({hasText:'原网页已载入画布'}).waitFor();
    assert.equal(page.url(),topUrl);assert.equal(await page.locator('#canvas > iframe.source-canvas-page').count(),1);
    assert.equal(await page.locator('#library').isVisible(),true);assert.equal(await page.locator('#inspector').isVisible(),true);
    await page.getByRole('button',{name:'撤销',exact:true}).click();
    assert.equal(await page.locator('#canvas iframe').count(),0);
    await page.getByRole('button',{name:'重做',exact:true}).click();
    await page.getByRole('status').filter({hasText:'原网页已载入画布'}).waitFor();
    const frame=page.frameLocator('#canvas iframe');
    assert.equal(await frame.locator('#go').evaluate(e=>getComputedStyle(e).backgroundColor),'rgb(39, 41, 45)');
    assert.equal(await page.evaluate(()=>window.business),false);
    const box=await frame.locator('#go').boundingBox();await page.mouse.click(box.x+box.width/2,box.y+box.height/2);
    assert.equal(await page.getByLabel('控件 ID',{exact:true}).inputValue(),'go');
    await page.getByLabel('显示文字',{exact:true}).fill('创建智能体');await page.getByLabel('显示文字',{exact:true}).press('Tab');
    assert.equal(await frame.locator('#go').innerText(),'+创建智能体');assert.equal(await frame.locator('#go i').count(),1);
    await page.getByLabel('宽度',{exact:true}).fill('210');await page.getByLabel('宽度',{exact:true}).press('Tab');
    assert.equal(await frame.locator('#go').evaluate(e=>Math.round(e.getBoundingClientRect().width)),210);
    await page.getByRole('button',{name:'撤销',exact:true}).click();
    assert.equal(await frame.locator('#go').evaluate(e=>Math.round(e.getBoundingClientRect().width)),156);
    const beforeMove=await frame.locator('#go').boundingBox();
    await page.mouse.move(beforeMove.x+20,beforeMove.y+20);await page.mouse.down();await page.mouse.move(beforeMove.x+60,beforeMove.y+40,{steps:4});await page.mouse.up();
    const moved=await frame.locator('#go').boundingBox();assert.ok(moved.x>beforeMove.x+25);
    await page.getByRole('button',{name:'按钮',exact:true}).click();
    assert.equal(await page.locator('#canvas .kind-button').count(),1);
    assert.equal(await frame.locator('#go').count(),1);
    await page.getByRole('button',{name:'生成 AI 开发任务',exact:true}).click();
    const task=await page.getByLabel('AI 开发任务',{exact:true}).inputValue();assert.match(task,/"sourcePage": true/);assert.match(task,/"source": true/);
    assert.equal(await page.evaluate(()=>window.sent.some(m=>['visual_save','design_workspace'].includes(m.action))),false);
    await page.getByRole('button',{name:'关闭',exact:true}).click();
    await page.screenshot({path:'../artifacts/design-workspace/original-in-canvas.png',fullPage:true});
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});

test('saved original-page drafts reload their background and apply edits without another import click',async()=>{
  const doc=newDesign();doc.sourcePage=true;
  const n=newNode('button',[]);Object.assign(n,{id:'saved-button',source:true,x:24,y:24,width:160,height:40,text:'原文'});
  doc.baseline=[structuredClone(n)];doc.nodes=[{...n,text:'保存后的标题',width:240}];
  const html='<html><body><button id="saved-button" style="position:absolute;left:24px;top:24px;width:160px;height:40px">原文</button></body></html>';
  const bundle=await build({entryPoints:['design-workspace.js'],bundle:true,format:'iife',write:false,loader:{'.md':'text','.css':'text'}});
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage();
    await page.evaluate(({doc,html})=>{
      let receive;window.__jadeDesignToken='reopen';window.chrome={webview:{addEventListener:(_,fn)=>receive=fn,postMessage:wire=>{
        if(!wire.startsWith('JADE_TOOL\t'))return;const [,id,action]=wire.split('\t');
        const values=action==='design_load'?[JSON.stringify(doc),html,'E:\\saved.e']:action==='design_edit_source'?[html,'https://jade-design-assets.invalid/']:['ok'];
        queueMicrotask(()=>receive({data:['JADE_TOOL_RESULT',id,'1',...values.map(encodeURIComponent)].join('\t')}));
      }}};
    },{doc,html});
    await page.addScriptTag({content:bundle.outputFiles[0].text});
    await page.getByRole('status').filter({hasText:'原网页已载入画布'}).waitFor();
    const control=page.frameLocator('#canvas iframe').locator('#saved-button');
    assert.equal(await control.innerText(),'保存后的标题');
    assert.equal(await control.evaluate(e=>Math.round(e.getBoundingClientRect().width)),240);
  }finally{await browser.close();}
});
