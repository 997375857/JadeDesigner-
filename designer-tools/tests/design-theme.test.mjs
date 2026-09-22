import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir} from 'node:fs/promises';
import {build} from 'esbuild';
import {chromium} from 'playwright';
import {newDesign,newComponent,buildRequest,themeOf,upgradeDefaultTheme} from '../design-model.js';

test('portable component skin is exported without replacing explicit draft colors',async()=>{
  const doc=newDesign(),skin=await readFile('jade-ui-theme.css','utf8');
  assert.equal(themeOf(doc).primary,'#27292d');
  doc.theme.primary='#aa3355';
  const task=buildRequest(doc,'FULL SPEC',skin);
  assert.match(task,/#aa3355/);assert.ok(task.includes(skin));assert.ok(task.includes('FULL SPEC'));
  assert.doesNotMatch(skin,/#canvas|#inspector|pointer-events\s*:\s*none/);
});

test('only untouched legacy defaults migrate to monochrome, custom themes remain unchanged',()=>{
  const doc=newDesign();doc.theme={primary:'#157f68',background:'#f7f9fa',surface:'#ffffff',text:'#24313b',border:'#d6dfe4',radius:8,font:'Microsoft YaHei UI',fontSize:14,motion:150};
  const custom=structuredClone(doc);custom.theme.primary='#112233';
  assert.equal(upgradeDefaultTheme(doc),true);assert.equal(doc.theme.primary,'#27292d');
  assert.equal(upgradeDefaultTheme(custom),false);assert.equal(custom.theme.primary,'#112233');
  assert.equal(upgradeDefaultTheme(doc),false);
});

test('neutral component gallery renders and exports the same reusable skin',async()=>{
  const doc=newDesign();doc.title='通用界面';doc.width=1040;doc.height=740;
  const placements=[
    ['sidebar',20,20,180,430],['heading',224,20,260,40],
    ['button',224,80,150,38],['outline-button',390,80,150,38],['tonal-button',556,80,150,38],['text-button',722,80,130,38],
    ['search',224,142,240,38],['select',480,142,180,38],['number',676,142,150,38],
    ['checkbox',224,206,140,36],['radio',380,206,140,36],['switch',536,206,140,36],
    ['progress',224,262,240,36],['slider',480,262,240,36],
    ['super-list',224,324,630,170],['tree',20,470,180,230],
    ['card',224,520,280,180],['dialog',528,520,300,200],['navigation-rail',904,80,90,400]
  ];
  for(const [key,x,y,width,height] of placements){const node=newComponent(key,doc.nodes,x,y);Object.assign(node,{width,height});doc.nodes.push(node);}
  const bundle=await build({entryPoints:['design-workspace.js'],bundle:true,format:'iife',write:false,loader:{'.md':'text','.css':'text'}});
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try{
    const page=await browser.newPage({viewport:{width:1800,height:1040}}),errors=[];
    page.on('pageerror',e=>errors.push(e.message));
    await page.evaluate(doc=>{
      let receive;window.__jadeDesignToken='theme-test';
      window.chrome={webview:{addEventListener:(_,fn)=>receive=fn,postMessage:wire=>{
        if(!wire.startsWith('JADE_TOOL\t'))return;
        const [,id,action]=wire.split('\t').map(decodeURIComponent);
        const fields=action==='design_load'?[JSON.stringify(doc),'','E:\\theme.e']:['ok'];
        queueMicrotask(()=>receive({data:['JADE_TOOL_RESULT',id,'1',...fields.map(encodeURIComponent)].join('\t')}));
      }}};
    },doc);
    await page.addScriptTag({content:bundle.outputFiles[0].text});
    await page.waitForFunction(()=>document.querySelector('#canvas .kind-button'));
    assert.equal(await page.locator('#canvas .jade-ui-control').count(),placements.length);
    assert.equal(await page.locator('#canvas .kind-button.variant-filled').first().evaluate(e=>getComputedStyle(e).backgroundColor),'rgb(39, 41, 45)');
    assert.equal(await page.locator('#canvas .kind-slider input').evaluate(e=>getComputedStyle(e).appearance),'none');
    assert.equal(await page.locator('#canvas .kind-number .field-affix svg').count(),2);
    assert.equal(await page.locator('#canvas .preset-dialog .layout-field').count(),2);
    assert.equal(await page.locator('#canvas .preset-sidebar .nav-icon svg').count(),4);
    await page.getByRole('button',{name:'生成 AI 开发任务',exact:true}).click();
    assert.ok((await page.getByLabel('AI 开发任务',{exact:true}).inputValue()).includes(await readFile('jade-ui-theme.css','utf8')));
    await page.getByRole('button',{name:'关闭',exact:true}).click();
    await mkdir('../artifacts/design-workspace',{recursive:true});
    await page.screenshot({path:'../artifacts/design-workspace/component-theme.png',fullPage:true});
    for(const viewport of [{width:1280,height:820},{width:390,height:844}]){
      await page.setViewportSize(viewport);
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
    }
    await page.screenshot({path:'../artifacts/design-workspace/component-theme-mobile.png',fullPage:true});
    assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});
