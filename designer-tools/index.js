import { createElement, X, RefreshCw, ArrowUpRight, Wrench, Undo2, Trash2, FileCode2, Stethoscope, Cable, Activity, ScanSearch } from 'lucide';
import { locateText, describeText } from './static-text.js';
import {createTrace} from './diagnostics-core.js';
import {createVisualDesigner} from './visual-designer.js';

// Embedded in InstallUiEventBridge's lexical scope; no per-page dependency.
const requests = new Map(); let requestId = 0;
const request = (kind, ...fields) => new Promise((resolve,reject) => {
  const id=String(++requestId);
  const timer=setTimeout(()=>{ requests.delete(id); reject(new Error('操作超时，请刷新检查状态，不要重复提交')); },20000);
  requests.set(id,{resolve,reject,timer});
  window.chrome.webview.postMessage(['JADE_TOOL',id,kind,...fields].map((s,i)=>i<3?s:encode(s)).join('\t'));
});
window.chrome.webview.addEventListener('message', e => {
  if(typeof e.data!=='string'||!e.data.startsWith('JADE_TOOL_RESULT\t')) return;
  const f=e.data.split('\t'), pending=requests.get(f[1]);
  if(!pending) return;
  requests.delete(f[1]); clearTimeout(pending.timer);
  try { const values=f.slice(3).map(decodeURIComponent); if(f[2]==='1') pending.resolve(values); else pending.reject(new Error(values[0]||'操作失败')); }
  catch(error) { pending.reject(error); }
});
const el=(tag,text,cls)=>{const n=document.createElement(tag); if(text) n.textContent=text; if(cls)n.className=cls; return n;};
const button=(text,fn)=>{const b=el('button',text); b.type='button'; b.addEventListener('click',fn); return b;};
const icon=(shape,title,fn)=>{const b=button('',fn); b.className='icon'; b.title=title; b.setAttribute('aria-label',title); b.append(createElement(shape)); return b;};
const style=el('style'); style.textContent=`
 *,*::before,*::after{box-sizing:border-box}
 [hidden]{display:none!important}
 .tools-bar{position:fixed;right:12px;bottom:60px;width:240px;max-width:calc(100vw - 24px);max-height:calc(100dvh - 72px);overflow:auto;display:flex;flex-direction:column;gap:4px;background:#fff;padding:8px;border:1px solid #acb7b0;border-radius:6px;box-shadow:0 4px 18px #0002}
 .tools-bar .tools-menu-head{display:flex;align-items:center;justify-content:space-between;padding:0 4px 4px;border-bottom:1px solid #e0e6e2}.tools-menu-head strong{font-size:14px}
 .tools-bar .command{display:flex;align-items:center;gap:10px;border:0;box-shadow:none;text-align:left;padding:9px 8px;min-height:38px}.command svg{width:18px;height:18px;flex:0 0 18px}
 .tools-bar .edit-controls{border-top:1px solid #e0e6e2;display:flex;align-items:center;gap:8px;padding-top:6px}.tools-bar label{margin:0;flex:1;min-width:0;padding:6px 8px}
 .tools-modes{display:flex;gap:0;border:1px solid #b9c8c1;border-radius:4px;overflow:hidden;margin:4px 0}.tools-modes button{flex:1;padding:7px 4px;border:0;border-radius:0;box-shadow:none}.tools-modes button[aria-checked=true]{background:#e2f2e8;color:#126d47}
 .tools-launcher.icon{width:40px;height:40px;position:relative;background:#fff}.tools-launcher[data-editing]{border-color:#16784b;background:#e2f2e8}.tools-launcher[data-editing]::after{content:'';position:absolute;top:3px;right:3px;width:6px;height:6px;background:#16784b;border-radius:50%}
 .tools-panel{position:fixed;right:12px;bottom:60px;width:min(920px,calc(100vw - 24px));height:min(680px,calc(100dvh - 72px));background:#fff;border:1px solid #9daea5;border-radius:6px;box-shadow:0 8px 28px #0003;display:flex;flex-direction:column;overflow:hidden;color:#25332d}
 .tools-head{padding:12px 16px;display:flex;align-items:center;gap:12px;border-bottom:1px solid #d9e0dc;flex-shrink:0}.tools-head strong{font-size:16px;flex:1;min-width:0;overflow-wrap:anywhere}
 .tools-body{padding:16px;overflow:auto;min-height:0;flex:1}.tools-foot{padding:10px 16px;display:flex;align-items:center;gap:12px;flex-wrap:wrap;border-top:1px solid #d9e0dc}
 .tools-panel button{box-shadow:none}.icon{padding:6px;width:32px;height:32px;flex:0 0 32px;display:inline-flex;align-items:center;justify-content:center}.icon svg{width:18px;height:18px}
 .tools-panel table{border-collapse:collapse;width:100%;font-size:13px;table-layout:fixed}.tools-panel th,.tools-panel td{padding:9px 6px;text-align:left;border-bottom:1px solid #e0e6e2;overflow-wrap:anywhere;vertical-align:top}.tools-panel th{background:#f4f6f5;position:sticky;top:0}.tools-panel th:last-child{width:78px}
 .tools-panel .actions{display:flex;gap:4px}.tools-panel .muted{color:#68736c;font-size:12px}.tools-panel .error{color:#aa382d;white-space:pre-wrap}.tools-panel pre{white-space:pre-wrap;overflow-wrap:anywhere;font:13px/1.6 Consolas,'Microsoft YaHei',monospace;margin:0}.tools-panel textarea{box-sizing:border-box;width:100%;height:260px;font:13px/1.6 Consolas,'Microsoft YaHei',monospace;border:1px solid #bbc5bf;padding:10px;border-radius:4px;resize:vertical}.tools-panel input[type=search]{font:inherit;padding:7px;border:1px solid #bbc5bf;border-radius:4px;width:220px;max-width:100%}
 .tools-panel table.health th:first-child{width:70px}.tools-panel table.health th:nth-child(2){width:180px}.tools-panel table.health th:last-child{width:auto}
 .tools-tabs{display:flex;gap:6px;margin:12px 0;flex-wrap:wrap}.tools-tabs button[aria-selected=true]{background:#e2f2e8;border-color:#16784b}.tools-panel .edited{color:#936100}
 .common-settings{padding:0 0 12px;border-bottom:1px solid #e0e6e2;margin-bottom:12px}.common-settings legend{font-weight:600}.common-settings input[type=text]{max-width:360px}.common-preview{margin-top:16px}
 @media(max-width:600px){.tools-body{padding:8px}.tools-head{padding:8px}.tools-panel table{min-width:600px}}
`;
commonRoot.append(style);
const panel=el('section',null,'tools-panel'); panel.hidden=true;
panel.setAttribute('role','dialog'); panel.setAttribute('aria-label','Jade 管理');
const head=el('div',null,'tools-head'), heading=el('strong'), body=el('div',null,'tools-body'), foot=el('div',null,'tools-foot');
head.append(heading,icon(X,'关闭面板',()=>closePanel())); panel.append(head,body,foot); commonRoot.append(panel);
const status=el('span'); status.setAttribute('role','status');
const closePanel=()=>{++scanEpoch;visual.deactivate();panel.hidden=true;launcher.focus();};
const open=title=>{setMenu(false);panel.classList.remove('visual-panel');panel.hidden=false; heading.textContent=title; body.replaceChildren(); foot.replaceChildren(status); status.textContent=''; status.className='';head.querySelector('button').focus();};
const fail=error=>{status.textContent=error.message||String(error); status.className='error';};
const busy=async(b,fn)=>{b.disabled=true; try{await fn();}catch(e){fail(e);}finally{b.disabled=false;}};
const observed=new Map();
const previewTrace=createTrace();
toolsIgnored=(element,reason)=>{
  const row=previewTrace.start('控件:'+readableName(element),'preview');
  previewTrace.finish(row,reason==='dialog_dismiss'?'dialog_dismiss':'window_control');
};
window.chrome.webview.addEventListener('message',event=>{
  if(typeof event.data!=='string'||!event.data.startsWith('JADE_TRACE_RESULT\t'))return;
  const fields=event.data.split('\t');
  const row=previewTrace.rows.find(r=>String(r.id)===fields[1]);
  if(row)previewTrace.finish(row,fields[2]==='1'?'designer_ok':'designer_error');
});
toolsObserve=(element,wire)=>{
  const saved=observed.get(element)||new Map();
  const f=wire.split('\t'); saved.set(f[9]||'',wire); observed.set(element,saved);
  const row=previewTrace.start(decodeURIComponent(f[9]||'')||'ui:'+readableName(element),'preview');
  return wire+'\t'+row.id;
};
const wireFor=element=>{
  const type=element.matches('select')?'select':element.matches('input[type=checkbox]')?'checkbox':element.matches('input[type=radio]')?'radio':'button';
  const suffix=type==='button'?'被单击':type==='select'?'选择项被改变':'选中状态被改变';
  const id=readableName(element), channel=element.dataset.jadeChannel||element.dataset.jadeEvent||'';
  return 'JADE_EVT\t'+[type==='button'?'click':'change',type,id,'','0',element.dataset.jadeHandler||(hasReadableName(element)?`${id}_${suffix}`:''),element.dataset.jadeAssembly||'',element.dataset.jadeCall||(channel?'JadeView.通讯.订阅':''),channel].map(encode).join('\t');
};
let scanEpoch=0;
async function manager() {
  open('事件绑定'); const epoch=++scanEpoch;
  const search=el('input'); search.type='search'; search.placeholder='筛选控件、回调或频道'; search.setAttribute('aria-label','筛选事件');
  const refresh=icon(RefreshCw,'重新检查',()=>manager());
  const toolbar=el('div',null,'tools-tabs'); toolbar.append(search,refresh); body.append(toolbar);
  const table=el('table'), thead=el('thead'), tr=el('tr');
  for(const title of ['网页控件','中文回调 / 程序集','频道','状态','操作'])tr.append(el('th',title));
  thead.append(tr); const tbody=el('tbody'); table.append(thead,tbody); body.append(table);
  search.addEventListener('input',()=>{for(const row of tbody.rows)row.hidden=!row.textContent.toLowerCase().includes(search.value.toLowerCase());});
  const controls=[...document.querySelectorAll('button,[role=button],input[type=button],input[type=submit],input[type=checkbox],input[type=radio],select')].slice(0,500);
  let count=0;
  for(const element of controls) {
    if(epoch!==scanEpoch) return;
    const wires=observed.has(element)?[...observed.get(element).values()]:[wireFor(element)];
    for(const wire of wires) {
      const f=wire.split('\t').map(decodeURIComponent), row=el('tr'), cells=Array.from({length:5},()=>el('td'));
      row.append(...cells); tbody.append(row);
      cells[0].append(el('div',(element.textContent||element.getAttribute('aria-label')||f[3]).trim().slice(0,80)),el('div',f[3],'muted'));
      cells[1].textContent=f[6]; cells[2].textContent=f[9]||'未点击，频道待确认';
      const known=observed.has(element)||!!(element.dataset.jadeChannel||element.dataset.jadeEvent);
      if(isWindowControl(element)) {cells[3].textContent='窗口控制，跳过'; continue;}
      if(!known) {cells[3].textContent='待观察'; continue;}
      try {
        const values=await request('inspect',wire);
        if(epoch!==scanEpoch) return;
        const update=values=>{
          const [state,message,assembly,handler,channel]=values;
          cells[1].replaceChildren(el('div',handler),el('div',assembly,'muted')); cells[2].textContent=channel; cells[3].textContent=message;
          cells[3].className='';cells[4].className='actions';cells[4].replaceChildren();
          const locate=icon(ArrowUpRight,'定位回调',()=>busy(locate,async()=>{const [note]=await request('locate',wire);if(epoch===scanEpoch)status.textContent=note;}));
          locate.disabled=!['complete','missing'].includes(state); cells[4].append(locate);
          if(state==='missing') {
            const repair=icon(Wrench,'只补缺失项',async()=>{
              repair.disabled=true;locate.disabled=true;cells[3].textContent='正在补齐';
              try {
                const [note]=await request('repair',wire);
                if(epoch!==scanEpoch)return;
                const refreshed=await request('inspect',wire);
                if(epoch!==scanEpoch)return;
                // Keep the row, filter, scroll and every unrelated request intact.
                update(refreshed);status.textContent=note;
              } catch(error) {
                if(epoch!==scanEpoch)return;
                cells[3].textContent=error.message;cells[3].className='error';
                repair.disabled=false;locate.disabled=false;
              }
            });cells[4].append(repair);
          }
        };
        update(values);
      }catch(error){cells[3].textContent=error.message;cells[3].className='error';}
      status.textContent=`已检查 ${++count} 项`;
    }
  }
  if(controls.length===500)status.textContent+='；当前最多显示 500 个控件';
}
const commonPreview=el('div',null,'common-preview');
function common() {
  ++scanEpoch;open('公共代码');
  commonFields.className='common-settings';
  commonPreview.replaceChildren();
  body.append(commonFields,commonButton,commonStatus,commonPreview);
}
async function previewCommon() {
  const epoch=++scanEpoch;commonPreview.replaceChildren();foot.replaceChildren(status);
  status.textContent='读取工程内存与模块方法…';
  commonButton.disabled=true;
  try {
    const [revision,ready,report,source,existing]=await request('common_preview',commonTray.checked?'1':'0',commonSingle.checked?'1':'0',commonId.value.trim());
    if(epoch!==scanEpoch)return;
    const reportNode=el('pre',report);commonPreview.append(reportNode);
    const tabs=el('div',null,'tools-tabs'), code=el('textarea'); code.readOnly=true;code.setAttribute('aria-label','公共代码源码');code.value=source;
    const views=[['新模板',source],['当前代码',existing||'当前尚无公共程序集']];
    for(const [label,value]of views){const tab=button(label,()=>{code.value=value;for(const b of tabs.children)b.setAttribute('aria-selected',String(b===tab));});tab.setAttribute('aria-selected',String(!tabs.childElementCount));tabs.append(tab);}
    commonPreview.append(tabs,code);
    const apply=button('确认生成',()=>busy(apply,async()=>{const [note]=await request('common_apply',revision);status.textContent=note;apply.hidden=true;}));
    apply.disabled=ready!=='1';foot.prepend(apply);
    status.textContent=ready==='1'?'检查通过，确认后才写入工程':'未写入工程；现有代码需对照合并，缺失依赖需先导入';
  }catch(e){if(epoch===scanEpoch)fail(e);}finally{commonButton.disabled=false;}
}
async function health() {
  const epoch=++scanEpoch;open('项目体检');status.textContent='只读检查中';
  try {
    const [report]=await request('health');if(epoch!==scanEpoch)return;
    const table=el('table',null,'health'),thead=el('thead'),head=el('tr'),tbody=el('tbody');
    for(const name of ['状态','检查项','结果'])head.append(el('th',name));thead.append(head);table.append(thead,tbody);body.append(table);
    const labels={ok:'正常',warning:'注意',error:'有问题',unknown:'未核验'};
    const add=(state,title,detail)=>{const row=el('tr');row.append(el('td',labels[state]||'未核验'),el('td',title),el('td',detail));if(state==='error')row.className='error';tbody.append(row);};
    for(const line of report.split('\n').filter(Boolean)){const [state,title,...detail]=line.split('\t');add(state,title,detail.join(' '));}
    const ids=new Map();for(const element of document.querySelectorAll('[id]'))if(element.id)ids.set(element.id,(ids.get(element.id)||0)+1);
    let duplicate=0;for(const [id,n]of ids)if(n>1){add('error','重复 DOM id',`${id}：${n} 个元素`);++duplicate;}
    if(!duplicate)add('ok','DOM id','当前页面没有重复 id');
    add('unknown','HTML 到回调','显式或已捕获频道可在“事件绑定”逐项核对；未触发的动态频道不猜测');
    status.textContent='检查完成，未修改源码；不是编译或运行通过证明';
    foot.prepend(icon(RefreshCw,'重新体检',health));
  }catch(error){fail(error);}
}
function diagnostics() {
  const epoch=++scanEpoch;open('通信诊断');
  body.append(el('p','设计器链路：网页事件 → 生成器处理 → 预览收到结果。不是运行程序的业务应答。','muted'));
  const table=el('table'),head=el('tr'),thead=el('thead'),tbody=el('tbody');
  for(const name of ['序号','频道 / 控件','设计器结果','耗时'])head.append(el('th',name));thead.append(head);table.append(thead,tbody);body.append(table);
  const render=()=>{
    tbody.replaceChildren();
    for(const row of previewTrace.rows.slice().reverse()) {
      const elapsed=row.state==='pending'?performance.now()-row.started:row.elapsed;
      const label=row.state==='dialog_dismiss'?'设计器忽略：弹窗关闭':row.state==='window_control'?'设计器忽略：窗口控制':row.state==='designer_ok'?'生成器已应答':row.state==='designer_error'?'生成器报告失败':elapsed>20000?'尚无应答，状态未知':'等待生成器';
      const tr=el('tr');for(const value of [row.id,row.channel,label,Math.round(elapsed)+' ms'])tr.append(el('td',String(value)));tbody.append(tr);
    }
  };
  render();const timer=setInterval(()=>{if(panel.hidden||epoch!==scanEpoch){clearInterval(timer);return;}render();},500);
  const install=button('接入运行诊断',()=>busy(install,async()=>{const [message]=await request('install_diagnostics');status.textContent=message;}));
  install.title='备份 HTML，新增本地诊断脚本和引用；运行程序后在左下角查看';
  foot.prepend(icon(Trash2,'清空预览诊断记录',()=>{previewTrace.clear();render();}),install);
  status.textContent='最多保留 200 条；不采集参数和应答正文';
}
commonButton.textContent='预览公共代码'; commonButton.addEventListener('click',previewCommon);
commonFields.addEventListener('input',()=>{++scanEpoch;commonPreview.replaceChildren();foot.replaceChildren(status);status.textContent='';});
const bar=el('div',null,'tools-bar');bar.id='designer-menu';bar.hidden=true;bar.setAttribute('aria-label','设计器工具菜单');bar.setAttribute('role','region');
const launcher=icon(Wrench,'设计器工具',()=>{if(!panel.hidden){closePanel();setMenu(true);}else setMenu(bar.hidden);});launcher.classList.add('tools-launcher');
launcher.setAttribute('aria-controls',bar.id);launcher.setAttribute('aria-expanded','false');
function setMenu(visible){bar.hidden=!visible;launcher.setAttribute('aria-expanded',String(visible));}
const menuHead=el('div',null,'tools-menu-head');menuHead.append(el('strong','Jade 工具'),icon(X,'收起工具',()=>{setMenu(false);launcher.focus();}));bar.append(menuHead);
const modeKey='jade-designer-mode:'+location.pathname;
const dockKey='jade-designer-native-toolbox:'+location.pathname;
let nativeDock=false,dockRevision=0;try{nativeDock=sessionStorage.getItem(dockKey)==='1';}catch{}
const dockChoice=el('input');dockChoice.type='checkbox';dockChoice.checked=nativeDock;
const syncDock=async()=>{
  const revision=++dockRevision;
  try{await request('visual_toolbox',interactionMode,nativeDock?'1':'0');}
  catch(error){if(revision!==dockRevision)return;nativeDock=false;dockChoice.checked=false;try{sessionStorage.removeItem(dockKey);}catch{}fail(error);}
};
let interactionMode='event';try{const saved=sessionStorage.getItem(modeKey);if(['design','event','preview'].includes(saved))interactionMode=saved;}catch{}
window.__jadeInteractionMode=interactionMode;
const modeGroup=el('div',null,'tools-modes');modeGroup.setAttribute('role','radiogroup');modeGroup.setAttribute('aria-label','操作模式');bar.append(modeGroup);
const modeButtons=new Map();
for(const [value,label]of [['design','设计'],['event','事件'],['preview','预览']]) {
  const b=button(label,()=>{setInteractionMode(value);closePanel();setMenu(false);});
  b.setAttribute('role','radio');b.setAttribute('aria-checked',String(value===interactionMode));modeGroup.append(b);modeButtons.set(value,b);
}
function setInteractionMode(value) {
  interactionMode=value;window.__jadeInteractionMode=value;editMode.checked=false;window.__jadeTextEditing=false;
  try{sessionStorage.setItem(modeKey,value);}catch{}
  launcher.toggleAttribute('data-editing',value==='design');launcher.title=value==='design'?'设计模式：只选中控件，Esc 返回预览':value==='preview'?'预览模式：不生成代码':'事件模式：点击控件生成或定位回调';
  for(const [mode,b]of modeButtons)b.setAttribute('aria-checked',String(mode===value));
  if(value!=='design')visual.deactivate();
  syncDock();
}
for(const [label,shape,fn]of [['可视化设计',Wrench,()=>{setInteractionMode('design');visual.open();}],['提取元素描述',ScanSearch,()=>{if(interactionMode!=='design')setInteractionMode('preview');closePanel();setMenu(false);visual.inspect();}],['公共代码',FileCode2,common],['项目体检',Stethoscope,health],['事件绑定',Cable,manager],['通信诊断',Activity,diagnostics]]){
  const b=button(label,fn);b.className='command';b.prepend(createElement(shape));bar.append(b);
}
const dockLabel=el('label');dockLabel.append(dockChoice,document.createTextNode('使用易语言组件箱'));bar.append(dockLabel);
dockChoice.addEventListener('change',()=>{
  nativeDock=dockChoice.checked;try{sessionStorage.setItem(dockKey,nativeDock?'1':'0');}catch{}
  if(nativeDock){setInteractionMode('design');closePanel();setMenu(false);}else{visual.arm('pointer');syncDock();}
});
const editLabel=el('label'), editMode=el('input');editMode.type='checkbox';editLabel.append(editMode,document.createTextNode('编辑文字'));
editMode.title='开启后双击静态文字，关闭后恢复网页操作';
const updateEditMode=()=>{const editing=editMode.checked;setInteractionMode(editing?'design':'event');visual.deactivate();editMode.checked=editing;window.__jadeTextEditing=editing;launcher.toggleAttribute('data-editing',editing);launcher.title=editing?'设计器工具：文字编辑中，Esc 退出':'设计器工具';};
editMode.addEventListener('change',()=>{updateEditMode();closePanel();setMenu(false);});
const undo=icon(Undo2,'撤销本次会话最后一次文字修改',()=>{++scanEpoch;open('撤销文字修改');busy(undo,async()=>{await request('text_undo');status.textContent='文字修改已撤销';});});
const editControls=el('div',null,'edit-controls');editControls.append(editLabel,undo);bar.append(editControls);commonRoot.append(bar,launcher);
const isTool=e=>e.composedPath().includes(commonHost);
const visual=createVisualDesigner({root:commonRoot,panel,body,foot,status,open,request,isTool,isDesign:()=>interactionMode==='design',notify:(text,failed)=>showCreationNotice(text,failed)});
window.chrome.webview.addEventListener('message',event=>{
  if(typeof event.data!=='string'||!event.data.startsWith('JADE_NATIVE_TOOLBOX\t'))return;
  const kind=event.data.slice('JADE_NATIVE_TOOLBOX\t'.length);
  if(kind==='unavailable'){
    nativeDock=false;dockChoice.checked=false;try{sessionStorage.removeItem(dockKey);}catch{}
    visual.arm('pointer');open('组件箱');fail(new Error('网页组件箱已关闭或不可用，已取消接管'));
  }else if(nativeDock&&interactionMode==='design'){window.__jadeTextEditing=false;editMode.checked=false;visual.arm(kind);}
});
setInteractionMode(interactionMode);
window.addEventListener('pointerdown',e=>{if(!isTool(e))setMenu(false);},true);
window.addEventListener('keydown',e=>{
  if(e.key!=='Escape')return;
  if(bar.hidden&&panel.hidden&&interactionMode!=='design'&&!window.__jadeElementPicking)return;
  e.preventDefault();e.stopImmediatePropagation();setMenu(false);closePanel();if(interactionMode==='design')setInteractionMode('preview');
},true);
for(const name of ['pointerdown','pointerup','mousedown','mouseup','click','change','submit','keydown']) window.addEventListener(name,e=>{
  if(interactionMode==='design'&&!isTool(e)){e.preventDefault();e.stopImmediatePropagation();}
},true);
window.addEventListener('dblclick',async e=>{
  if(!window.__jadeTextEditing||isTool(e))return;
  e.preventDefault();e.stopImmediatePropagation();++scanEpoch;open('修改网页文字');
  try{
    let target=e.target;
    if(target?.closest('i,svg')) target=target.closest('button,label')||target;
    const description=describeText(target);
    const [revision,source]=await request('text_open');
    const range=locateText(source,description);
    const input=el('input');input.type='text';input.maxLength=1000;input.value=description.text.trim();input.setAttribute('aria-label','新文字');
    body.append(el('p',description.text.trim()),input);input.focus();input.select();
    const save=button('保存到 HTML',()=>busy(save,async()=>{
      if(!description.node.isConnected||description.node.nodeValue!==description.text)throw new Error('页面文字已改变，请重新选择');
      const leading=description.text.match(/^\s*/)[0],trailing=description.text.match(/\s*$/)[0];
      const replacement=leading+input.value+trailing;
      await request('text_save',revision,String(range.start),String(range.end),range.old,replacement);
      description.node.nodeValue=replacement;status.textContent='已保存并备份，图标与事件绑定未修改';save.hidden=true;
    }));foot.prepend(save);
    input.addEventListener('keydown',event=>{if(event.key==='Enter'){event.preventDefault();save.click();}});
  }catch(error){fail(error);}
},true);
