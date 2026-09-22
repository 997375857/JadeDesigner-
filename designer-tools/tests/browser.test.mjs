import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir} from 'node:fs/promises';
import {build} from 'esbuild';
import {chromium} from 'playwright';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
import {existsSync} from 'node:fs';

async function bridgeScript() {
  const cpp=await readFile(new URL('../../src/WebPreview.cpp',import.meta.url),'utf8');
  const bundle=await build({entryPoints:[fileURLToPath(new URL('../index.js',import.meta.url))],bundle:true,format:'iife',write:false});
  const start=cpp.indexOf('const std::wstring script = LR"JS(')+'const std::wstring script = LR"JS('.length;
  return cpp.slice(start,cpp.indexOf(')JS";',start)).replace(')JS" + DesignerToolsScript() + LR"JS(',bundle.outputFiles[0].text);
}
async function tool(page,name) {
  if(!(await page.locator('.tools-bar').isVisible()))await page.getByRole('button',{name:'设计器工具',exact:true}).click();
  await page.getByRole('button',{name,exact:true}).click();
}
async function editing(page,value) {
  await page.getByRole('button',{name:'设计器工具',exact:true}).click();
  await page.getByRole('checkbox',{name:'编辑文字'}).setChecked(value);
}

test('original-page design preserves layout, edits source, adds controls and does not trigger business clicks',async()=>{
  const script=await bridgeScript(),browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage({viewport:{width:1200,height:850}}),errors=[];
    page.on('pageerror',e=>errors.push(e.message));
    await page.exposeFunction('nativeTemplate',(kind,number)=>execFileSync(fileURLToPath(new URL('../../bin/tests/DesignerToolsTests.exe',import.meta.url)),['--standard',kind,number],{encoding:'utf8'}));
    const source='<!doctype html><html><head><style>body{margin:30px;font:14px sans-serif;background:white}#area{width:580px;padding:30px;border:1px solid #eee}#go{width:150px;height:38px;background:#27292d;color:white;border:0;border-radius:6px}</style></head><body><section id="area"><button id="go" data-jade-handler="已有回调" data-jade-channel="ui:test">新建智能体</button></section></body></html>';
    await page.setContent(source);const before=await page.locator('#go').boundingBox();
    await page.evaluate(source=>{
      window.__jadeSourceDesign=true;window.business=0;window.messages=[];window.listeners=[];window.savedHtml=source;window.rev=1;
      document.querySelector('#go').addEventListener('click',()=>++window.business);
      window.chrome={webview:{addEventListener:(_,fn)=>window.listeners.push(fn),postMessage:async wire=>{
        window.messages.push(wire);const f=wire.split('\t');if(f[0]!=='JADE_TOOL')return;
        const args=f.slice(3).map(decodeURIComponent);let values=['ok'],ok=true;
        try{
          if(f[2]==='text_open')values=[String(window.rev),window.savedHtml];
          if(f[2]==='visual_name')values=[args[1]];
          if(f[2]==='visual_save'){
            if(args[0]!==String(window.rev))throw Error('revision mismatch');
            const patches=[];
            for(const line of args[1].split('\n')){
              const [kind,s,e,old,key,value]=line.split('\t').map(decodeURIComponent),start=Number(s),end=Number(e);
              if(window.savedHtml.slice(start,end)!==old)throw Error('range mismatch');
              let text;if(kind==='insert')text='\n'+await window.nativeTemplate(key,value)+'\n'+old;
              else if(kind==='text'){const span=document.createElement('span');span.textContent=value;text=span.innerHTML;}
              else throw Error('unexpected edit');
              patches.push({start,end,text});
            }
            for(const p of patches.sort((a,b)=>b.start-a.start))window.savedHtml=window.savedHtml.slice(0,p.start)+p.text+window.savedHtml.slice(p.end);
            values=[String(++window.rev),window.savedHtml];
          }
        }catch(error){ok=false;values=[error.message];}
        queueMicrotask(()=>window.listeners.forEach(fn=>fn({data:['JADE_TOOL_RESULT',f[1],ok?'1':'0',...values.map(encodeURIComponent)].join('\t')})));
      }}};
    },source);
    await page.evaluate(script);
    assert.equal(await page.evaluate(()=>window.__jadeInteractionMode),'design');
    assert.deepEqual(await page.locator('#go').boundingBox(),before);
    await page.locator('#go').click();
    await page.getByRole('textbox',{name:'标题文字',exact:true}).fill('创建智能体');
    await page.getByRole('button',{name:'保存属性',exact:true}).click();
    await page.getByText('属性已保存并备份，原有回调和脚本保持不变',{exact:true}).waitFor();
    assert.equal(await page.locator('#go').innerText(),'创建智能体');
    assert.equal(await page.locator('#go').getAttribute('data-jade-handler'),'已有回调');
    assert.equal(await page.locator('#go').evaluate(e=>getComputedStyle(e).backgroundColor),'rgb(39, 41, 45)');
    await page.getByRole('tab',{name:'控件',exact:true}).click();
    await page.locator('.visual-palette').getByRole('button',{name:'按钮',exact:true}).click();
    await page.locator('#area #jade_button_1').waitFor();
    assert.equal(await page.locator('#jade_button_1').evaluate(e=>getComputedStyle(e).backgroundColor),'rgb(39, 41, 45)');
    assert.equal(await page.evaluate(()=>window.business),0);
    assert.equal(await page.evaluate(()=>window.messages.filter(w=>w.startsWith('JADE_EVT')).length),0);
    await page.getByRole('button',{name:'设计器工具',exact:true}).click();
    assert.equal(await page.getByRole('radio',{name:'预览',exact:true}).isDisabled(),true);
    await page.getByRole('button',{name:'返回设计稿',exact:true}).click();
    await page.waitForFunction(()=>window.messages.some(w=>w.split('\t')[2]==='design_workspace'));
    await page.screenshot({path:'../artifacts/design-workspace/source-design.png',fullPage:true});
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});

test('native diagnostics bridge captures preview requests and responses without code generation',async()=>{
  const script=await bridgeScript(),browser=await chromium.launch({channel:'msedge',headless:true});
  try {
    const page=await browser.newPage(),errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.setContent('<button id="test" data-jade-channel="app:test" data-jade-handler="测试被单击">测试</button>');
    await page.evaluate(()=>{
      window.messages=[];
      window.chrome={webview:{addEventListener:()=>{},postMessage:wire=>window.messages.push(wire)}};
      document.querySelector('button').onclick=()=>window.jade.invoke('app:test',JSON.stringify({text:'中文',n:2}));
    });
    await page.evaluate(script);
    await page.evaluate(()=>{window.__jadeInteractionMode='preview';});
    await page.locator('#test').click();
    await page.waitForFunction(()=>window.messages.filter(w=>w.startsWith('JADE_DOCK_TRACE\t')).length===2);
    const messages=await page.evaluate(()=>window.messages);
    const records=messages.filter(w=>w.startsWith('JADE_DOCK_TRACE\t')).map(w=>w.split('\t').slice(1).map(decodeURIComponent));
    assert.equal(records[0][1],'测试被单击');
    assert.deepEqual(JSON.parse(records[0][3]),{text:'中文',n:2});
    assert.equal(JSON.parse(records[1][4]).jadePreview,true);
    assert.match(records[1][5],/未调用易语言/);
    assert.equal(messages.filter(w=>w.startsWith('JADE_EVT\t')).length,0);
    assert.deepEqual(errors,[]);
  } finally {await browser.close();}
});

test('control menus: list root selection, event contract, custom channels and read-only preview',async()=>{
  const script=await bridgeScript(),browser=await chromium.launch({channel:'msedge',headless:true});
  try {
    const page=await browser.newPage({viewport:{width:1100,height:800}}),errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.setContent(`<!doctype html><style>body{margin:20px}section{display:inline-block;width:400px;margin:8px;vertical-align:top}
      [role=grid]{border:1px solid #bbb;height:300px}header,[role=row]{padding:15px}.blank{height:160px}button{padding:12px}</style>
      <button id="business" data-jade-channel="app:import" data-jade-handler="导入_被单击"><span>导入</span></button>
      <section><div id="list-a" data-jade-control="super-list" data-jade-id="主播列表" data-jade-handler="原单击" role="grid">
        <header>主播列表</header><div role="row" data-jade-id="row-1"><span role="gridcell" id="cell-1">主播甲</span></div><div class="blank"></div></div></section>
      <section><div id="list-b" data-jade-control="list" data-jade-id="弹幕列表" role="grid"><header>弹幕列表</header><div class="blank"></div></div></section>
      <div id="duplicate" data-jade-id="重复" data-jade-control="super-list">重复列表</div><span data-jade-id="重复"></span>`);
    await page.evaluate(()=>{
      window.messages=[];window.listeners=[];window.business=0;
      document.querySelector('#business').addEventListener('contextmenu',()=>++window.business);
      window.chrome={webview:{addEventListener:(name,fn)=>window.listeners.push(fn),postMessage:wire=>{
        window.messages.push(wire);const fields=wire.split('\t');if(fields[0]!=='JADE_TOOL')return;
        const failed=fields[2]==='control_event'&&window.failEvent;
        setTimeout(()=>window.listeners.forEach(fn=>fn({data:['JADE_TOOL_RESULT',fields[1],failed?'0':'1',encodeURIComponent(failed?'绑定冲突，未修改':'事件操作完成')].join('\t')})),0);
      }}};
    });
    await page.evaluate(script);
    const right=async selector=>page.locator(selector).click({button:'right'});
    const requests=()=>page.evaluate(()=>window.messages.filter(w=>w.split('\t')[2]==='control_event').map(w=>w.split('\t').slice(3).map(decodeURIComponent)));
    await right('#cell-1');
    assert.equal(await page.locator('.jade-context-menu').getByRole('menuitem').count(),10);
    assert.ok((await page.locator('.jade-context-menu').innerText()).includes('主播列表'));
    await page.getByRole('menuitem',{name:'创建或跳转：被双击',exact:true}).click();
    await page.locator('jade-created-notice').getByText('事件操作完成',{exact:true}).waitFor();
    assert.deepEqual((await requests())[0],['super-list','主播列表','dblclick','原单击','','','']);
    const native=await page.evaluate(()=>window.messages.filter(w=>w.startsWith('JADE_VISUAL_SELECTION')).at(-1).split('\t').slice(1).map(decodeURIComponent));
    assert.deepEqual(native.slice(0,2),['super-list','主播列表']);
    await right('#list-a .blank');await page.getByRole('menuitem',{name:'创建或跳转：表项被选中',exact:true}).click();
    await page.waitForFunction(()=>window.messages.filter(w=>w.split('\t')[2]==='control_event').length===2);
    await right('#list-b header');await page.getByRole('menuitem',{name:'创建或跳转：被单击',exact:true}).click();
    await page.waitForFunction(()=>window.messages.filter(w=>w.split('\t')[2]==='control_event').length===3);
    assert.deepEqual((await requests())[2].slice(0,3),['super-list','弹幕列表','click']);
    await right('#business span');
    assert.equal(await page.getByRole('menuitem',{name:'创建或跳转：被双击',exact:true}).count(),0);
    await page.getByRole('menuitem',{name:'创建或跳转：被单击',exact:true}).click();
    await page.waitForFunction(()=>window.messages.filter(w=>w.split('\t')[2]==='control_event').length===4);
    assert.deepEqual((await requests())[3],['button','business','click','导入_被单击','app:import','','']);
    assert.equal(await page.evaluate(()=>window.business),0);
    await right('#duplicate');
    assert.equal(await page.getByRole('menuitem',{name:'创建或跳转：被单击',exact:true}).isDisabled(),true);
    await page.keyboard.press('Escape');assert.equal(await page.locator('.jade-context-menu').isVisible(),false);
    const mode=async name=>{
      if(!(await page.locator('.tools-bar').isVisible()))await page.getByRole('button',{name:'设计器工具',exact:true}).click();
      await page.getByRole('radio',{name,exact:true}).click();
    };
    await mode('预览');await right('#list-a .blank');
    assert.equal(await page.getByRole('menuitem',{name:'创建或跳转：被单击',exact:true}).isDisabled(),true);
    await page.getByRole('menuitem',{name:'复制控件 ID',exact:true}).click();
    await page.locator('jade-created-notice').getByText('控件 ID 已复制：主播列表',{exact:true}).waitFor();
    assert.equal((await requests()).length,4);
    assert.ok(await page.evaluate(()=>window.messages.some(w=>w.split('\t')[2]==='copy_description'&&decodeURIComponent(w.split('\t')[3])==='主播列表')));
    await mode('设计');
    await page.locator('#list-a .blank').click();
    const picked=await page.evaluate(()=>window.messages.filter(w=>w.startsWith('JADE_VISUAL_SELECTION')).at(-1).split('\t').slice(1).map(decodeURIComponent));
    assert.equal(picked[1],'主播列表');assert.equal(picked[11],'1');
    await page.evaluate(()=>window.failEvent=true);await right('#list-a .blank');
    await page.getByRole('menuitem',{name:'创建或跳转：右键单击',exact:true}).click();
    await page.locator('jade-created-notice').getByText('绑定冲突，未修改',{exact:true}).waitFor();
    for(const viewport of [{width:1100,height:800},{width:390,height:600},{width:600,height:260}]) {
      await page.setViewportSize(viewport);
      await page.locator('#list-a').dispatchEvent('contextmenu',{clientX:viewport.width-5,clientY:viewport.height-5});
      // The browser commits resize and delivers the menu's clamp on the next frame.
      await page.waitForFunction(()=>{
        const r=document.querySelector('jade-common-tools').shadowRoot.querySelector('.jade-context-menu').getBoundingClientRect();
        return r.x>=0&&r.y>=0&&r.right<=innerWidth&&r.bottom<=innerHeight;
      },null,{timeout:2000});
      const bounds=await page.locator('.jade-context-menu').boundingBox();
      const layout=await page.locator('.jade-context-menu').evaluate(n=>({left:n.style.left,top:n.style.top,width:innerWidth,height:innerHeight,host:n.getRootNode().host.getBoundingClientRect().toJSON()}));
      assert.ok(bounds.x>=0&&bounds.y>=0&&bounds.x+bounds.width<=viewport.width&&bounds.y+bounds.height<=viewport.height,JSON.stringify({viewport,bounds,layout}));
      await mkdir(new URL('../../bin/tests/screenshots/',import.meta.url),{recursive:true});
      await page.screenshot({path:fileURLToPath(new URL(`../../bin/tests/screenshots/control-events-${viewport.width}.png`,import.meta.url))});
    }
    assert.deepEqual(errors,[]);
    assert.equal(await page.evaluate(()=>window.messages.filter(w=>w.startsWith('JADE_EVT')).length),0);
  } finally {await browser.close();}
});

test('embedded bridge: manager, preview, edit isolation and responsive layout',async()=>{
  const script=await bridgeScript();
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1000}});
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    const source='<!doctype html><html><head><meta charset="utf-8"></head><body><h1>测试窗口</h1><button id="go" data-jade-handler="导入_被单击" data-jade-channel="app:import"><i></i>导入账号</button><button id="dynamic">待观察</button><button id="conflict" data-jade-channel="app:duplicate">冲突按钮</button></body></html>';
    await page.setContent(source);
    await page.evaluate(source=>{
      window.business=0;window.messages=[];window.listener=[];
      document.querySelector('#go').addEventListener('click',()=>++window.business);
      window.chrome={webview:{addEventListener:(name,fn)=>window.listener.push(fn),postMessage:wire=>{
        window.messages.push(wire);
        const f=wire.split('\t');
        if(f[0]==='JADE_EVT'){setTimeout(()=>window.listener.forEach(fn=>fn({data:'JADE_TRACE_RESULT\t'+f[10]+'\t1\tcreate_background'})),0);return;}
        if(f[0]!=='JADE_TOOL')return;
        const a=f.slice(3).map(decodeURIComponent);let values=[];
        if(f[2]==='text_open')values=['1',source];
        else if(f[2]==='text_save')values=['2',source];
        else if(f[2]==='inspect')values=a[0].includes('duplicate')?['conflict','同频道冲突','程序集','冲突回调','app:duplicate']:[window.repaired?'complete':'missing',window.repaired?'已完整绑定':'缺少订阅','程序集','导入_被单击','app:import'];
        else if(f[2]==='repair'){window.repaired=true;values=['已补齐'];}
        else if(f[2]==='common_preview')values=['1','0','缺少模块，未写入','新模板','已有内容'];
        else if(f[2]==='health')values=['ok\tIDE 兼容性\t通过\nunknown\t启动接入\t未单独核验'];
        else values=['成功'];
        setTimeout(()=>window.listener.forEach(fn=>fn({data:['JADE_TOOL_RESULT',f[1],'1',...values.map(encodeURIComponent)].join('\t')})),0);
      }}};
    },source);
    await page.evaluate(script);
    assert.equal(await page.locator('.tools-bar').isVisible(),false);
    assert.equal(await page.locator('.tools-panel').isVisible(),false);
    assert.equal(await page.getByRole('button',{name:'预览公共代码',exact:true}).count(),0);
    await tool(page,'事件绑定');
    await page.getByText('同频道冲突',{exact:true}).waitFor();
    assert.equal(await page.getByRole('button',{name:'只补缺失项'}).count(),1);
    assert.equal(await page.locator('.tools-panel').getByText('待观察',{exact:true}).count(),2);
    await page.getByRole('searchbox',{name:'筛选事件'}).fill('导入');
    await page.locator('.tools-panel tbody').evaluate(n=>{window.originalRows=n;window.originalRow=n.children[0];});
    const inspections=await page.evaluate(()=>window.messages.filter(x=>x.split('\t')[2]==='inspect').length);
    await page.getByRole('button',{name:'只补缺失项'}).click();
    await page.getByText('已完整绑定',{exact:true}).waitFor();
    assert.equal(await page.getByRole('searchbox',{name:'筛选事件'}).inputValue(),'导入');
    assert.ok(await page.locator('.tools-panel tbody').evaluate(n=>n===window.originalRows&&n.children[0]===window.originalRow));
    assert.equal(await page.evaluate(()=>window.messages.filter(x=>x.split('\t')[2]==='inspect').length),inspections+1);
    assert.equal(await page.getByRole('button',{name:'只补缺失项'}).count(),0);
    await tool(page,'公共代码');
    await page.getByRole('checkbox',{name:'托盘常驻'}).check();
    await page.getByRole('button',{name:'预览公共代码',exact:true}).click();
    await page.getByText('缺少模块，未写入',{exact:true}).waitFor();
    assert.equal(await page.getByRole('button',{name:'确认生成'}).isDisabled(),true);
    const options=await page.evaluate(()=>window.messages.find(x=>x.split('\t')[2]==='common_preview').split('\t').slice(3));
    assert.equal(options[0],'1');
    await editing(page,true);
    await page.locator('#go').dblclick();
    await page.getByRole('textbox',{name:'新文字',exact:true}).fill('导入新账号');
    await page.getByRole('button',{name:'保存到 HTML'}).click();
    await page.getByText('已保存并备份，图标与事件绑定未修改',{exact:true}).waitFor();
    assert.equal(await page.locator('#go').innerText(),'导入新账号');
    assert.equal(await page.locator('#go i').count(),1);
    assert.equal(await page.evaluate(()=>window.business),0);
    assert.equal(await page.evaluate(()=>window.messages.filter(x=>x.startsWith('JADE_EVT')).length),0);
    const save=await page.evaluate(()=>window.messages.find(x=>x.startsWith('JADE_TOOL')&&x.split('\t')[2]==='text_save').split('\t').slice(3).map(decodeURIComponent));
    assert.equal(source.slice(Number(save[1]),Number(save[2])),save[3]);
    assert.equal(save[4],'导入新账号');
    await editing(page,false);
    await page.locator('#go').click();
    await page.waitForFunction(()=>window.messages.some(x=>x.startsWith('JADE_EVT')));
    assert.equal(await page.evaluate(()=>window.business),1);
    await tool(page,'项目体检');
    await page.getByText('未单独核验',{exact:true}).waitFor();
    await mkdir(new URL('../../bin/tests/screenshots/',import.meta.url),{recursive:true});
    for(const viewport of [{width:1440,height:1000},{width:812,height:572},{width:390,height:844},{width:812,height:300}]) {
      await page.setViewportSize(viewport);
      await tool(page,'事件绑定');
      await page.getByText('同频道冲突',{exact:true}).waitFor();
      await page.waitForTimeout(80);
      const bounds=await page.locator('.tools-panel').boundingBox();
      assert.ok(bounds.x>=0&&bounds.y>=0&&bounds.x+bounds.width<=viewport.width&&bounds.y+bounds.height<=viewport.height);
      assert.equal(await page.locator('.tools-bar').isVisible(),false);
      const launcher=await page.locator('.tools-launcher').boundingBox();
      assert.ok(bounds.y+bounds.height<=launcher.y);
      assert.ok(await page.locator('.tools-panel svg').count()>0);
      await page.screenshot({path:fileURLToPath(new URL(`../../bin/tests/screenshots/tools-${viewport.width}.png`,import.meta.url))});
      await tool(page,'项目体检');
      await page.getByText('未单独核验',{exact:true}).waitFor();
      await page.screenshot({path:fileURLToPath(new URL(`../../bin/tests/screenshots/health-${viewport.width}.png`,import.meta.url))});
    }
    assert.deepEqual(errors,[]);
  } finally {await browser.close();}
});

test('collapsed tools leave the page clickable; menu and settings never stack',async()=>{
  const script=await bridgeScript();
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try {
    const page=await browser.newPage();
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.setContent('<!doctype html><html><head><style>body{margin:0;font:15px sans-serif}.log{position:fixed;right:0;top:0;bottom:0;width:320px;background:#f6f8f7;padding:20px;box-sizing:border-box}.log button{position:absolute;bottom:105px;right:260px;padding:10px;white-space:nowrap}#modal{display:none;position:fixed;inset:0;background:#0004;align-items:center;justify-content:center}#modal.show{display:flex}#modal button{padding:18px;background:white}</style></head><body><aside class="log"><h2>事件记录</h2><p>测试打开一级弹窗_被单击</p><button id="page-action">页面操作</button></aside><div id="modal" class="modal"><button id="dismiss">确定</button></div></body></html>');
    await page.evaluate(()=>{
      window.clicks=0;window.messages=[];
      document.querySelector('#page-action').onclick=()=>{++window.clicks;document.querySelector('#modal').classList.add('show');};
      document.querySelector('#dismiss').onclick=()=>document.querySelector('#modal').classList.remove('show');
      window.chrome={webview:{addEventListener:()=>{},postMessage:wire=>window.messages.push(wire)}};
    });
    await page.evaluate(script);
    for(const viewport of [{width:812,height:572},{width:390,height:844},{width:812,height:300}]) {
      await page.setViewportSize(viewport);
      const host=await page.locator('jade-common-tools').boundingBox();
      assert.equal(host.width,40);assert.equal(host.height,40);
      assert.equal(await page.locator('.tools-panel').isVisible(),false);
      assert.equal(await page.locator('.tools-bar').isVisible(),false);
      await page.screenshot({path:fileURLToPath(new URL(`../../bin/tests/screenshots/collapsed-${viewport.width}-${viewport.height}.png`,import.meta.url))});
      await page.locator('#page-action').click();
      await page.locator('#dismiss').click();
      await page.getByRole('button',{name:'设计器工具',exact:true}).click();
      const menu=await page.locator('.tools-bar').boundingBox();
      assert.ok(menu.x>=0&&menu.y>=0&&menu.x+menu.width<=viewport.width&&menu.y+menu.height<=host.y);
      // Closing the menu with an outside click must not swallow that click.
      await page.mouse.click(viewport.width-5,viewport.height/2);
      assert.equal(await page.locator('.tools-bar').isVisible(),false);
      await page.locator('#page-action').click();
      await page.locator('#dismiss').click();
      await tool(page,'公共代码');
      assert.equal(await page.locator('.tools-bar').isVisible(),false);
      assert.equal(await page.getByRole('group',{name:'公共代码设置'}).isVisible(),true);
      const pane=await page.locator('.tools-panel').boundingBox();
      assert.ok(pane.x>=0&&pane.y>=0&&pane.x+pane.width<=viewport.width&&pane.y+pane.height<=host.y);
      await page.screenshot({path:fileURLToPath(new URL(`../../bin/tests/screenshots/common-${viewport.width}-${viewport.height}.png`,import.meta.url))});
      await page.keyboard.press('Escape');
      assert.equal(await page.locator('.tools-panel').isVisible(),false);
    }
    assert.equal(await page.evaluate(()=>window.clicks),6);
    await editing(page,true);
    assert.equal(await page.locator('.tools-launcher').getAttribute('data-editing'),'');
    await page.keyboard.press('Escape');
    assert.equal(await page.evaluate(()=>window.__jadeTextEditing),false);
    await page.locator('#page-action').click();
    assert.equal(await page.evaluate(()=>window.clicks),7);
    assert.deepEqual(errors,[]);
  } finally {await browser.close();}
});

test('runtime diagnostics observes real promises without payload logging',async()=>{
  const runtime=await build({entryPoints:[fileURLToPath(new URL('../runtime-diagnostics.js',import.meta.url))],bundle:true,format:'iife',write:false});
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage({viewport:{width:900,height:700}});
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.setContent('<html><head></head><body><button id="go">业务按钮</button></body></html>');
    await page.evaluate(()=>{
      window.calls=0;window.result={ok:false};window.jade={invoke(channel,payload,options){++window.calls;window.argumentsPreserved=payload.secret==='private'&&options.timeout===27;return Promise.resolve(window.result);}};
    });
    await page.evaluate(runtime.outputFiles[0].text);
    const unchanged=await page.evaluate(async()=>await window.jade.invoke('app:test',{secret:'private'},{timeout:27})===window.result);
    assert.ok(unchanged);assert.equal(await page.evaluate(()=>window.calls),1);assert.ok(await page.evaluate(()=>window.argumentsPreserved));
    await page.getByRole('button',{name:'通信诊断',exact:true}).click();
    await page.getByText('应答到达（非业务成功判定）',{exact:true}).waitFor();
    assert.ok(!(await page.locator('jade-runtime-diagnostics').innerText()).includes('private'));
    await page.evaluate(()=>{window.__jadeDesignerPreview=true;return window.jade.invoke('ui:preview',{secret:'private'},{timeout:27});});
    await page.getByText('预览模拟 · 应答到达（非业务成功判定）',{exact:true}).waitFor();
    await page.setViewportSize({width:390,height:844});
    const box=await page.getByRole('dialog',{name:'通信诊断',exact:true}).boundingBox();
    assert.ok(box.x>=0&&box.y>=0&&box.x+box.width<=390&&box.y+box.height<=844);
    await page.screenshot({path:fileURLToPath(new URL('../../bin/tests/screenshots/runtime-390.png',import.meta.url))});
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});

const nativeVisual=fileURLToPath(new URL('../../bin/tests/DesignerToolsTests.exe',import.meta.url));

test('design mode and selection never open a dismissed visual panel, including late replies',async()=>{
  const script=await bridgeScript(),browser=await chromium.launch({channel:'msedge',headless:true});
  try {
    const page=await browser.newPage({viewport:{width:1000,height:700}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
    const source='<html><body><button id="go">按钮</button></body></html>';
    await page.route('https://jade.test/**',route=>route.fulfill({contentType:'text/html; charset=utf-8',body:source}));
    await page.addInitScript(source=>{
      window.messages=[];window.listeners=[];window.business=0;
      document.addEventListener('click',e=>{if(e.target.id==='go')++window.business;});
      window.chrome={webview:{addEventListener:(_,fn)=>window.listeners.push(fn),postMessage:wire=>{
        window.messages.push(wire);const f=wire.split('\t');if(f[0]!=='JADE_TOOL')return;
        const values=f[2]==='text_open'?['1',source]:['done'];
        const reply=()=>window.listeners.forEach(fn=>fn({data:['JADE_TOOL_RESULT',f[1],'1',...values.map(encodeURIComponent)].join('\t')}));
        if(f[2]==='text_open'&&window.delayOpen)window.replyOpen=reply;else queueMicrotask(reply);
      }}};
    },source);
    await page.goto('https://jade.test/index.html');await page.evaluate(script);
    const mode=async name=>{
      const radio=page.getByRole('radio',{name,exact:true});
      if(!await radio.isVisible())await page.getByRole('button',{name:'设计器工具',exact:true}).click();
      await radio.click();
    };
    await mode('预览');await mode('设计');
    assert.equal(await page.locator('.tools-bar').isVisible(),true);
    await page.getByRole('button',{name:'收起工具'}).click();
    await page.locator('#go').click();
    assert.equal(await page.locator('.tools-panel').isVisible(),false);
    assert.equal(await page.evaluate(()=>window.business),0);
    assert.equal(await page.evaluate(()=>window.messages.filter(w=>w.split('\t')[2]==='text_open').length),0);
    await tool(page,'可视化设计');await page.getByRole('textbox',{name:'标题文字',exact:true}).waitFor();
    await page.getByRole('button',{name:'关闭面板'}).click();await page.locator('#go').click();
    assert.equal(await page.locator('.tools-panel').isVisible(),false);
    await page.evaluate(()=>window.delayOpen=true);await tool(page,'可视化设计');await page.waitForFunction(()=>typeof window.replyOpen==='function');
    await page.getByRole('button',{name:'关闭面板'}).click();await page.evaluate(()=>window.replyOpen());await page.waitForTimeout(50);
    assert.equal(await page.locator('.tools-panel').isVisible(),false);
    await page.reload();await page.evaluate(script);assert.equal(await page.evaluate(()=>window.__jadeInteractionMode),'design');
    await page.locator('#go').click();assert.equal(await page.locator('.tools-panel').isVisible(),false);
    await mode('预览');await page.locator('#go').click();assert.equal(await page.evaluate(()=>window.business),1);
    assert.equal(await page.evaluate(()=>window.messages.filter(w=>w.startsWith('JADE_EVT')).length),0);
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});

test('element descriptions identify static and dynamic targets without firing business or exposing form values',async()=>{
  const script=await bridgeScript(),browser=await chromium.launch({channel:'msedge',headless:true});
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1000}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
    const source='<!doctype html><html><head><style>body{font:16px sans-serif;padding:24px}button{padding:12px;color:rgb(15,90,60)}</style></head><body><section id="area"><button id="go" data-jade-handler="导入账号_被单击" data-jade-channel="app:import">导入账号</button><input id="password" type="password" value="secret-never-copy"><input id="account" value="private-current-value"></section></body></html>';
    await page.setContent(source);
    await page.evaluate(source=>{
      window.messages=[];window.listeners=[];window.business=0;window.downs=0;
      document.querySelector('#go').onclick=()=>++window.business;
      document.querySelector('#go').onmousedown=()=>++window.downs;
      window.chrome={webview:{addEventListener:(_,fn)=>window.listeners.push(fn),postMessage:wire=>{
        window.messages.push(wire);const f=wire.split('\t');if(f[0]!=='JADE_TOOL')return;
        const values=f[2]==='text_open'?['1',source,'E:\\测试工程\\web\\index.html']:['done'];
        queueMicrotask(()=>window.listeners.forEach(fn=>fn({data:['JADE_TOOL_RESULT',f[1],'1',...values.map(encodeURIComponent)].join('\t')})));
      }}};
    },source);await page.evaluate(script);
    await tool(page,'提取元素描述');assert.equal(await page.evaluate(()=>window.__jadeInteractionMode),'preview');
    await page.locator('#go').click();
    const description=page.getByRole('textbox',{name:'元素描述',exact:true});
    await page.waitForFunction(()=>document.querySelector('jade-common-tools').shadowRoot.querySelector('.element-description')?.value.includes('已与当前 HTML 核对'));
    const text=await description.inputValue();assert.ok(text.includes('导入账号_被单击')&&text.includes('app:import')&&text.includes('#go')&&text.includes('index.html'));
    assert.ok(!text.includes('secret-never-copy')&&!text.includes('private-current-value'));
    assert.equal(await page.evaluate(()=>window.business+window.downs),0);
    await page.getByRole('textbox',{name:'修改要求',exact:true}).fill('把按钮改成蓝色，保留事件');
    await page.getByRole('button',{name:'复制给 AI',exact:true}).click();await page.getByText('元素描述已复制',{exact:true}).waitFor();
    assert.ok(await page.evaluate(()=>window.messages.filter(w=>w.split('\t')[2]==='copy_description').map(w=>decodeURIComponent(w.split('\t')[3])).some(s=>s.includes('把按钮改成蓝色'))));
    for(const viewport of [{width:1440,height:1000},{width:812,height:572},{width:390,height:844}]) {
      await page.setViewportSize(viewport);
      const r=await page.locator('.visual-panel').boundingBox();assert.ok(r.x>=0&&r.y>=0&&r.x+r.width<=viewport.width&&r.y+r.height<=viewport.height);
      assert.equal(await page.locator('.tools-body').evaluate(n=>n.scrollWidth>n.clientWidth),false);
      await page.screenshot({path:fileURLToPath(new URL(`../../bin/tests/screenshots/description-${viewport.width}.png`,import.meta.url))});
    }
    await page.setViewportSize({width:1440,height:1000});await page.getByRole('button',{name:'关闭面板'}).click();await page.locator('#go').click();
    assert.equal(await page.evaluate(()=>window.business),1);assert.equal(await page.evaluate(()=>window.downs),1);
    await tool(page,'可视化设计');await page.locator('#go').click();
    await page.getByRole('tab',{name:'AI 描述',exact:true}).click();assert.ok((await description.inputValue()).includes('#go'));
    assert.equal(await page.evaluate(()=>window.business),1);
    await page.evaluate(()=>{const button=document.createElement('button');button.id='dynamic';button.textContent='运行时按钮';button.onclick=()=>++window.business;document.querySelector('#area').append(button);});
    await tool(page,'提取元素描述');await page.locator('#dynamic').click();
    await page.waitForFunction(()=>document.querySelector('jade-common-tools').shadowRoot.querySelector('.element-description')?.value.includes('仅运行时观察'));
    assert.ok((await description.inputValue()).includes('#dynamic'));
    assert.equal(await page.evaluate(()=>window.business),1);
    await page.getByRole('button',{name:'点选元素',exact:true}).click();await page.keyboard.press('Escape');
    assert.equal(await page.evaluate(()=>window.__jadeElementPicking),false);
    await tool(page,'提取元素描述');await page.locator('#password').click();
    assert.ok(!(await description.inputValue()).includes('secret-never-copy'));
    assert.equal(await page.evaluate(()=>window.messages.filter(w=>w.startsWith('JADE_EVT')).length),0);
    assert.equal(await page.evaluate(()=>window.messages.filter(w=>['visual_save','repair','text_save'].includes(w.split('\t')[2])).length),0);
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});
test('repair preserves a long binding list and modes survive page reload',async()=>{
  const script=await bridgeScript(),browser=await chromium.launch({channel:'msedge',headless:true});
  try {
    const page=await browser.newPage({viewport:{width:1000,height:700}});
    const source='<!doctype html><html><body>'+Array.from({length:120},(_,i)=>`<button id="item${i}" data-jade-channel="test:${i}" data-jade-handler="组${i}_被单击">组${i}</button>`).join('')+'</body></html>';
    await page.route('https://jade.test/**',route=>route.fulfill({contentType:'text/html; charset=utf-8',body:source}));
    await page.addInitScript(source=>{
      window.messages=[];window.listeners=[];window.fixed=new Set();window.business=0;
      document.addEventListener('click',()=>++window.business);
      window.chrome={webview:{addEventListener:(_,fn)=>window.listeners.push(fn),postMessage:wire=>{
        window.messages.push(wire);const f=wire.split('\t');if(f[0]!=='JADE_TOOL')return;
        const args=f.slice(3).map(decodeURIComponent),event=(args[0]||'').split('\t').map(decodeURIComponent);
        if(f[2]==='repair')window.fixed.add(event[3]);
        const values=f[2]==='text_open'?['1',source]:f[2]==='inspect'?[window.fixed.has(event[3])?'complete':'missing',window.fixed.has(event[3])?'已完整绑定':'缺少订阅','程序集',event[6],event[9]]:['已补齐'];
        queueMicrotask(()=>window.listeners.forEach(fn=>fn({data:['JADE_TOOL_RESULT',f[1],'1',...values.map(encodeURIComponent)].join('\t')})));
      }}};
    },source);
    await page.goto('https://jade.test/index.html');await page.evaluate(script);
    await tool(page,'事件绑定');await page.getByText('已检查 120 项',{exact:true}).waitFor();
    await page.getByRole('searchbox',{name:'筛选事件'}).fill('组');
    const row=page.locator('.tools-panel tbody tr').nth(70);
    await row.scrollIntoViewIfNeeded();
    const before=await page.locator('.tools-body').evaluate(n=>{window.listBody=n;window.savedRow=n.querySelectorAll('tbody tr')[70];return n.scrollTop;});
    assert.ok(before>0);
    await row.getByRole('button',{name:'只补缺失项'}).click();await row.getByText('已完整绑定',{exact:true}).waitFor();
    assert.equal(await page.getByRole('searchbox',{name:'筛选事件'}).inputValue(),'组');
    assert.ok(await page.locator('.tools-body').evaluate(n=>n===window.listBody&&n.querySelectorAll('tbody tr')[70]===window.savedRow));
    assert.ok(Math.abs((await page.locator('.tools-body').evaluate(n=>n.scrollTop))-before)<=1);
    assert.equal(await page.evaluate(()=>window.messages.filter(w=>w.split('\t')[2]==='inspect').length),121);
    const mode=async name=>{await page.getByRole('button',{name:'设计器工具',exact:true}).click();await page.getByRole('radio',{name,exact:true}).click();};
    await mode('设计');await page.reload();await page.evaluate(script);
    assert.equal(await page.evaluate(()=>window.__jadeInteractionMode),'design');
    const count=await page.evaluate(()=>window.business);await page.locator('#item0').click();
    assert.equal(await page.evaluate(()=>window.business),count);
    assert.equal(await page.evaluate(()=>window.messages.filter(w=>w.startsWith('JADE_EVT')).length),0);
    await mode('预览');await page.reload();await page.evaluate(script);
    assert.equal(await page.evaluate(()=>window.__jadeInteractionMode),'preview');
    await page.locator('#item0').click();assert.equal(await page.evaluate(()=>window.business),1);
    assert.equal(await page.evaluate(()=>window.messages.filter(w=>w.startsWith('JADE_EVT')).length),0);
  }finally{await browser.close();}
});

test('visual properties, native control templates, isolated modes and responsive sidebar', {skip:!existsSync(nativeVisual)},async()=>{
  const script=await bridgeScript();
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try {
    const page=await browser.newPage({viewport:{width:1440,height:1000}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.exposeFunction('nativeTemplate',(kind,number)=>execFileSync(nativeVisual,['--standard',kind,number],{encoding:'utf8'}));
    const source='<!doctype html><html><head><meta charset="utf-8"><style>body{font:15px sans-serif;padding:24px}#area{max-width:650px;padding:24px;border:1px solid #aaa}button{padding:8px 14px}h1{font-size:24px}</style></head><body><h1>界面设计测试</h1><section id="area"><div class="grid"><button id="go" style="transform:translateX(3px)" data-jade-channel="app:import" data-jade-handler="原回调"><i></i><span>原标题</span></button><input id="account" type="text" placeholder="账号"><select id="method"><option value="a">方式甲</option><option value="b">方式乙</option></select></div></section><section id="second"><div class="grid"><button id="other">其他区域</button></div></section></body></html>';
    await page.setContent(source);
    await page.evaluate(source=>{
      window.savedHtml=source;window.rev=0;window.messages=[];window.listeners=[];window.business=0;window.invocations=0;
      document.querySelector('#go').addEventListener('click',()=>++window.business);
      window.chrome={webview:{addEventListener:(name,fn)=>window.listeners.push(fn),postMessage:async wire=>{
        window.messages.push(wire);const f=wire.split('\t');if(f[0]!=='JADE_TOOL')return;
        const args=f.slice(3).map(decodeURIComponent);let values=[],ok=true;
        try {
          if(f[2]==='text_open')values=[String(++window.rev),window.savedHtml];
          else if(f[2]==='visual_name')values=[args[1]];
          else if(f[2]==='visual_save') {
            if(window.failSave)throw new Error('文件已被外部修改');
            if(args[0]!==String(window.rev))throw new Error('stale revision');
            const patches=new Map(),old=window.savedHtml;
            for(const line of args[1].split('\n')) {
              const [kind,s,e,before,key,value]=line.split('\t').map(decodeURIComponent),start=Number(s),end=Number(e);
              if(old.slice(start,end)!==before)throw new Error('stale range');
              if(kind==='text'){const holder=document.createElement('span');holder.textContent=value;patches.set(start,{end,next:holder.innerHTML});}
              else if(kind==='insert')patches.set(start,{end,next:'\n'+await window.nativeTemplate(key,value)+'\n'+before});
              else {
                let patch=patches.get(start);
                if(!patch){const template=document.createElement('template');template.innerHTML=before;patch={end,node:template.content.firstElementChild};patches.set(start,patch);}
                if(kind==='style')patch.node.setAttribute('style',(patch.node.getAttribute('style')||'')+';'+key+':'+value+' !important;');
                else if(['disabled','readonly','checked','selected'].includes(key))patch.node.toggleAttribute(key,value==='1');
                else patch.node.setAttribute(key,value);
              }
            }
            let next=old;
            for(const [start,patch]of [...patches].sort(([a],[b])=>b-a)) {
              if(patch.node){const whole=patch.node.outerHTML,tag=patch.node.localName;patch.next=['input','img'].includes(tag)?whole:whole.slice(0,whole.length-tag.length-3);}
              next=next.slice(0,start)+patch.next+next.slice(patch.end);
            }
            window.beforeUndo=old;window.savedHtml=next;values=[String(++window.rev),next];
          } else values=['done'];
        } catch(error){ok=false;values=[error.message];}
        setTimeout(()=>window.listeners.forEach(fn=>fn({data:['JADE_TOOL_RESULT',f[1],ok?'1':'0',...values.map(encodeURIComponent)].join('\t')})),0);
      }}};
    },source);
    await page.evaluate(script);
    const mode=async name=>{if(!(await page.locator('.tools-bar').isVisible()))await page.getByRole('button',{name:'设计器工具',exact:true}).click();await page.getByRole('radio',{name,exact:true}).click();};
    await mode('设计');assert.equal(await page.locator('.tools-panel').isVisible(),false);
    await page.locator('#go').click();assert.equal(await page.locator('.tools-panel').isVisible(),false);
    await tool(page,'可视化设计');
    await page.getByRole('textbox',{name:'标题文字',exact:true}).waitFor();
    assert.equal(await page.evaluate(()=>window.business),0);assert.equal(await page.evaluate(()=>window.messages.filter(w=>w.startsWith('JADE_EVT')).length),0);
    await page.getByRole('textbox',{name:'标题文字',exact:true}).fill('开始导入');
    await page.getByRole('combobox',{name:'宽度',exact:true}).selectOption('fixed');
    await page.getByRole('spinbutton',{name:'宽度像素',exact:true}).fill('180');
    await page.getByRole('spinbutton',{name:'字号',exact:true}).fill('20');
    await page.getByRole('button',{name:'保存属性',exact:true}).click();
    await page.getByText('属性已保存并备份，原有回调和脚本保持不变',{exact:true}).waitFor();
    assert.equal(await page.locator('#go').innerText(),'开始导入');assert.equal(await page.locator('#go i').count(),1);
    assert.equal(await page.locator('#go').getAttribute('data-jade-handler'),'原回调');assert.equal(await page.locator('#go').evaluate(e=>Math.round(e.getBoundingClientRect().width)),180);
    const beforeMove=await page.locator('#go').boundingBox();
    await page.getByRole('spinbutton',{name:'步长',exact:true}).fill('10');
    await page.getByRole('button',{name:'右移',exact:true}).click();await page.getByRole('button',{name:'上移',exact:true}).click();
    assert.equal(await page.getByRole('spinbutton',{name:'横向偏移',exact:true}).inputValue(),'10');
    assert.equal(await page.getByRole('spinbutton',{name:'纵向偏移',exact:true}).inputValue(),'-10');
    assert.deepEqual(await page.locator('#go').boundingBox(),beforeMove);
    await page.getByRole('button',{name:'保存属性',exact:true}).click();await page.getByText('属性已保存并备份，原有回调和脚本保持不变',{exact:true}).waitFor();
    const afterMove=await page.locator('#go').boundingBox();assert.equal(afterMove.x-beforeMove.x,10);assert.equal(afterMove.y-beforeMove.y,-10);
    assert.equal(await page.locator('#go').evaluate(n=>n.style.transform),'translateX(3px)');
    assert.ok(await page.evaluate(()=>window.savedHtml.includes('translate:10px -10px')));
    await page.getByRole('button',{name:'重置位移',exact:true}).click();await page.getByRole('button',{name:'保存属性',exact:true}).click();
    await page.getByText('属性已保存并备份，原有回调和脚本保持不变',{exact:true}).waitFor();assert.deepEqual(await page.locator('#go').boundingBox(),beforeMove);
    await page.evaluate(()=>window.failSave=true);await page.getByRole('textbox',{name:'标题文字',exact:true}).fill('不能落盘');await page.getByRole('button',{name:'保存属性',exact:true}).click();
    await page.getByText('文件已被外部修改',{exact:true}).waitFor();assert.equal(await page.locator('#go').innerText(),'开始导入');await page.evaluate(()=>window.failSave=false);
    await page.getByRole('tab',{name:'控件',exact:true}).click();
    assert.equal(await page.locator('.visual-palette button').count(),10);
    await page.locator('.visual-palette').getByRole('button',{name:'按钮',exact:true}).click();
    await page.getByText('按钮已添加并备份，事件模式下可生成回调',{exact:true}).waitFor();
    assert.equal(await page.locator('#jade_button_1').getAttribute('data-jade-handler'),'界面按钮1_被单击');
    assert.equal(await page.locator('#area #jade_button_1').count(),1);
    assert.equal(await page.locator('#area > .grid > #jade_button_1').count(),1);
    assert.equal(await page.locator('#second .grid').innerHTML(),'<button id="other">其他区域</button>');
    await page.getByRole('tab',{name:'控件',exact:true}).click();await page.locator('.visual-palette').getByRole('button',{name:'按钮',exact:true}).click();
    await page.locator('#jade_button_2').waitFor();
    await page.getByRole('button',{name:'设计器工具',exact:true}).click();
    await page.getByRole('checkbox',{name:'使用易语言组件箱',exact:true}).check();
    assert.equal(await page.locator('.tools-panel').isVisible(),false);
    assert.ok(await page.evaluate(()=>window.messages.some(w=>w.split('\t')[2]==='visual_toolbox'&&w.endsWith('\tdesign\t1'))));
    await page.evaluate(()=>window.listeners.forEach(fn=>fn({data:'JADE_NATIVE_TOOLBOX\tbutton'})));
    assert.equal(await page.locator('#jade_button_3').count(),0);
    await page.locator('#area').click({position:{x:8,y:8}});await page.locator('#area #jade_button_3').waitFor();
    assert.equal(await page.locator('#jade_button_3').getAttribute('data-jade-handler'),'界面按钮3_被单击');
    assert.equal(await page.evaluate(()=>window.business),0);
    assert.equal(await page.locator('.tools-panel').isVisible(),false);
    await page.evaluate(()=>{window.failSave=true;window.listeners.forEach(fn=>fn({data:'JADE_NATIVE_TOOLBOX\tbutton'}));});
    await page.locator('#area').click({position:{x:8,y:8}});
    await page.locator('jade-created-notice').getByText('文件已被外部修改',{exact:true}).waitFor();
    assert.equal(await page.locator('.tools-panel').isVisible(),false);assert.equal(await page.locator('#jade_button_4').count(),0);
    await page.evaluate(()=>window.failSave=false);
    await page.evaluate(()=>window.listeners.forEach(fn=>fn({data:'JADE_NATIVE_TOOLBOX\tbutton'})));
    await mode('预览');await page.locator('#go').click();assert.equal(await page.evaluate(()=>window.business),1);assert.equal(await page.evaluate(()=>window.messages.filter(w=>w.startsWith('JADE_EVT')).length),0);
    assert.equal(await page.locator('#jade_button_4').count(),0);
    assert.ok(await page.evaluate(()=>window.messages.some(w=>w.split('\t')[2]==='visual_toolbox'&&w.endsWith('\tpreview\t1'))));
    await page.evaluate(()=>window.jade.invoke=async()=>{++window.invocations;return {};});await page.locator('#jade_button_1').click();assert.equal(await page.evaluate(()=>window.invocations),1);
    await mode('事件');await page.locator('#go').click();await page.waitForFunction(()=>window.messages.some(w=>w.startsWith('JADE_EVT')));assert.equal(await page.evaluate(()=>window.business),2);
    assert.ok(await page.evaluate(()=>window.messages.some(w=>w.split('\t')[2]==='visual_toolbox'&&w.endsWith('\tevent\t1'))));
    await mode('设计');assert.equal(await page.locator('.tools-panel').isVisible(),false);await tool(page,'可视化设计');
    for(const viewport of [{width:1440,height:1000},{width:812,height:572},{width:390,height:844}]) {
      await page.setViewportSize(viewport);
      const bounds=await page.locator('.visual-panel').boundingBox();assert.ok(bounds.x>=0&&bounds.y>=0&&bounds.x+bounds.width<=viewport.width&&bounds.y+bounds.height<=viewport.height);
      const overflow=await page.locator('.visual-panel .tools-body').evaluate(n=>n.scrollWidth>n.clientWidth);assert.equal(overflow,false);
      await page.screenshot({path:fileURLToPath(new URL(`../../bin/tests/screenshots/visual-${viewport.width}.png`,import.meta.url))});
    }
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});
