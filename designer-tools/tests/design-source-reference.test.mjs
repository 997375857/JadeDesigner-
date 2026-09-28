import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {existsSync} from 'node:fs';
import path from 'node:path';
import {build} from 'esbuild';
import {chromium} from 'playwright';

for(const [name,root,screenshot]of [
  ['AI reference','E:/易语言软件源码/AI获客/web','ai-reference-in-canvas.png'],
  ['Baidu static text','E:/易语言软件源码/百度弹窗/百度弹窗/web','baidu-text-in-canvas.png']
])test(`local ${name} retains its original CSS and assets inside the design canvas`,{skip:!existsSync(path.join(root,'index.html'))},async()=>{
  const html=await readFile(path.join(root,'index.html'),'utf8');
  const bundle=await build({entryPoints:['design-workspace.js'],bundle:true,format:'iife',write:false,loader:{'.md':'text','.css':'text'}});
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage({viewport:{width:1800,height:1080}}),requested=[],errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.route('https://jade-design-assets.invalid/**',async route=>{
      const url=new URL(route.request().url()),file=path.resolve(root,'.'+decodeURIComponent(url.pathname));requested.push(url.pathname);
      if(!file.startsWith(path.resolve(root)+path.sep))return route.abort();
      try{const body=await readFile(file),type={'.css':'text/css','.woff2':'font/woff2','.woff':'font/woff','.png':'image/png','.svg':'image/svg+xml','.jpg':'image/jpeg'}[path.extname(file)]||'application/octet-stream';await route.fulfill({body,contentType:type});}catch{await route.fulfill({status:404,body:''});}
    });
    await page.evaluate(html=>{
      let receive;window.__jadeDesignToken='reference';window.chrome={webview:{addEventListener:(_,fn)=>receive=fn,postMessage:wire=>{
        if(!wire.startsWith('JADE_TOOL\t'))return;const [,id,action]=wire.split('\t');
        const values=action==='design_load'?['',html,'E:\\reference.e']:action==='design_edit_source'?[html,'https://jade-design-assets.invalid/']:['ok'];
        queueMicrotask(()=>receive({data:['JADE_TOOL_RESULT',id,'1',...values.map(encodeURIComponent)].join('\t')}));
      }}};
    },html);
    await page.addScriptTag({content:bundle.outputFiles[0].text});
    await page.getByRole('status').filter({hasText:'就绪'}).waitFor();
    await page.getByRole('button',{name:'编辑现有网页',exact:true}).click();
    await page.getByRole('status').filter({hasText:'原网页已载入画布'}).waitFor();
    assert.ok(requested.includes('/style.css'));
    assert.equal(requested.some(x=>x.endsWith('.js')),false);
    const original=page.frameLocator('#canvas iframe');
    if(name==='AI reference'){
      assert.ok(requested.includes('/ui-polish.css'));assert.equal(await original.locator('.top-nav').count(),1);
    }else{
      const title=original.getByText('百度弹窗助手',{exact:true}),box=await title.boundingBox();
      await page.mouse.click(box.x+box.width/2,box.y+box.height/2);
      assert.equal(await page.getByLabel('显示文字',{exact:true}).inputValue(),'百度弹窗助手');
      await page.getByRole('button',{name:'复制控件',exact:true}).click();
      assert.equal(await page.locator('#canvas .kind-label').innerText(),'百度弹窗助手');
      assert.equal(await page.locator('#canvas .kind-label').evaluate(e=>e.scrollHeight<=e.clientHeight&&e.scrollWidth<=e.clientWidth),true);
      await page.getByLabel('顶边',{exact:true}).fill('490');await page.getByLabel('顶边',{exact:true}).press('Tab');
      const plan=original.getByText('弹窗计划',{exact:true}),planBox=await plan.boundingBox();
      await page.mouse.click(planBox.x+planBox.width/2,planBox.y+planBox.height/2);
      assert.equal(await page.getByLabel('显示文字',{exact:true}).inputValue(),'弹窗计划');
      await page.getByLabel('显示文字',{exact:true}).fill('新的弹窗计划');await page.getByLabel('显示文字',{exact:true}).press('Tab');
      assert.equal(await original.getByText('新的弹窗计划',{exact:true}).count(),1);
      assert.equal(await title.getAttribute('id'),null);
      await page.getByRole('button',{name:'添加文字',exact:true}).click();
      await page.getByLabel('显示文字',{exact:true}).fill('新增文字');await page.getByLabel('显示文字',{exact:true}).press('Tab');
      await page.getByLabel('顶边',{exact:true}).fill('540');await page.getByLabel('顶边',{exact:true}).press('Tab');
    }
    await page.screenshot({path:'../artifacts/design-workspace/'+screenshot,fullPage:true});
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});
