import { createElement, X, RefreshCw, ArrowUpRight, Wrench, Undo2, Trash2, FileCode2, Stethoscope, Cable, Activity, ScanSearch, Copy } from 'lucide';
import { locateText, describeText } from './static-text.js';
import {createTrace} from './diagnostics-core.js';
import {createNativeDiagnostics} from './native-diagnostics.js';
import {createVisualDesigner} from './visual-designer.js';
import {controlKind,metadataFor,controlTarget,stableControlId,uniqueControlId} from './jade-control-metadata.js';

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
// Older builds rendered communication diagnostics as a floating WebView
// panel. Remove that legacy surface when the new native 工作夹 page loads;
// otherwise an already-open preview can keep showing the obsolete columns.
document.querySelectorAll('jade-runtime-diagnostics,.diagnostics-panel').forEach(node=>node.remove());
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
 .tools-panel.diagnostics-panel{left:12px;right:auto;width:min(760px,calc(100vw - 24px));height:min(460px,calc(100dvh - 72px))}
 .tools-head{padding:12px 16px;display:flex;align-items:center;gap:12px;border-bottom:1px solid #d9e0dc;flex-shrink:0}.tools-head strong{font-size:16px;flex:1;min-width:0;overflow-wrap:anywhere}
 .tools-body{padding:16px;overflow:auto;min-height:0;flex:1}.tools-foot{padding:10px 16px;display:flex;align-items:center;gap:12px;flex-wrap:wrap;border-top:1px solid #d9e0dc}
 .tools-panel button{box-shadow:none}.icon{padding:6px;width:32px;height:32px;flex:0 0 32px;display:inline-flex;align-items:center;justify-content:center}.icon svg{width:18px;height:18px}
 .tools-panel table{border-collapse:collapse;width:100%;font-size:13px;table-layout:fixed}.tools-panel th,.tools-panel td{padding:9px 6px;text-align:left;border-bottom:1px solid #e0e6e2;overflow-wrap:anywhere;vertical-align:top}.tools-panel th{background:#f4f6f5;position:sticky;top:0}.tools-panel th:last-child{width:78px}
  .tools-panel .actions{display:flex;gap:4px}.tools-panel .muted{color:#68736c;font-size:12px}.tools-panel .error{color:#aa382d;white-space:pre-wrap}.tools-panel pre{white-space:pre-wrap;overflow-wrap:anywhere;font:13px/1.6 Consolas,'Microsoft YaHei',monospace;margin:0}.tools-panel textarea{box-sizing:border-box;width:100%;height:260px;font:13px/1.6 Consolas,'Microsoft YaHei',monospace;border:1px solid #bbc5bf;padding:10px;border-radius:4px;resize:vertical}.tools-panel input[type=search]{font:inherit;padding:7px;border:1px solid #bbc5bf;border-radius:4px;width:220px;max-width:100%}
  .jade-context-menu{position:fixed;z-index:2147483001;width:320px;max-width:calc(100vw - 24px);max-height:calc(100vh - 24px);box-sizing:border-box;overflow:auto;padding:6px;background:#fff;border:1px solid #9daea5;border-radius:6px;box-shadow:0 8px 24px #0003;color:#25332d}
  .jade-context-menu[hidden]{display:none}.jade-context-menu strong{display:block;padding:7px 9px;border-bottom:1px solid #e0e6e2;font-size:13px;overflow-wrap:anywhere}
  .jade-context-menu .context-note{padding:6px 9px;color:#68736c;font-size:12px;overflow-wrap:anywhere}.jade-context-menu button{display:block;width:100%;padding:8px 9px;border:0;border-radius:4px;box-shadow:none;text-align:left;font:inherit;color:inherit;background:transparent;cursor:pointer}
  .jade-context-menu button:hover{background:#e8f4ed}.jade-context-menu button:disabled{color:#9aa59f;cursor:wait}
 .tools-panel table.health th:first-child{width:70px}.tools-panel table.health th:nth-child(2){width:180px}.tools-panel table.health th:last-child{width:auto}
 .tools-tabs{display:flex;gap:6px;margin:12px 0;flex-wrap:wrap}.tools-tabs button[aria-selected=true]{background:#e2f2e8;border-color:#16784b}.tools-panel .edited{color:#936100}
 .common-settings{padding:0 0 12px;border-bottom:1px solid #e0e6e2;margin-bottom:12px}.common-settings legend{font-weight:600}.common-settings input[type=text]{max-width:360px}.common-preview{margin-top:16px}
 @media(max-width:600px){.tools-body{padding:8px}.tools-head{padding:8px}.tools-panel table{min-width:600px}}
`;
commonRoot.append(style);
const contextMenu=el('div',null,'jade-context-menu');contextMenu.hidden=true;contextMenu.setAttribute('role','menu');commonRoot.append(contextMenu);
const panel=el('section',null,'tools-panel'); panel.hidden=true;
panel.setAttribute('role','dialog'); panel.setAttribute('aria-label','Jade 管理');
const head=el('div',null,'tools-head'), heading=el('strong'), body=el('div',null,'tools-body'), foot=el('div',null,'tools-foot');
head.append(heading,icon(X,'关闭面板',()=>closePanel())); panel.append(head,body,foot); commonRoot.append(panel);
const status=el('span'); status.setAttribute('role','status');
const closePanel=()=>{++scanEpoch;visual.deactivate();panel.hidden=true;launcher.focus();};
const open=title=>{setMenu(false);panel.classList.remove('visual-panel','diagnostics-panel');panel.hidden=false; heading.textContent=title; body.replaceChildren(); foot.replaceChildren(status); status.textContent=''; status.className='';head.querySelector('button').focus();};
const fail=error=>{status.textContent=error.message||String(error); status.className='error';};
const busy=async(b,fn)=>{b.disabled=true; try{await fn();}catch(e){fail(e);}finally{b.disabled=false;}};
const observed=new Map();
const previewTrace=createTrace();
const nativeDiagnostics=createNativeDiagnostics(
  message=>window.chrome.webview.postMessage(message),
  channel=>{
    const element=[...document.querySelectorAll('[data-jade-channel]')]
      .find(node=>node.dataset.jadeChannel===channel);
    return element?{handler:element.dataset.jadeHandler||'',assembly:element.dataset.jadeAssembly||''}:{};
  },()=>!!window.__jadeDesignerPreview);
toolsCaptureInvoke=nativeDiagnostics.wrap;
let previewDebug=false;
toolsPreviewTrace=(fields,payload)=>{
  const shared=window.__jadeRuntimeDiagnostics?.trace;
  const row=shared
    ? [...shared.rows].reverse().find(item=>item.mode==='preview'&&item.channel===(fields[8]||'ui:'+fields[2])&&item.payload===(payload||'')&&performance.now()-item.started<1000)||shared.start(fields[8]||'ui:'+fields[2],payload||'','preview')
    : previewTrace.start(fields[8]||'ui:'+fields[2],payload||'', 'preview');
  row.domEvent=fields[0];row.controlType=fields[1];row.elementId=fields[2];
  row.value=fields[3];row.checked=fields[4];row.handler=fields[5];row.assembly=fields[6];
  row.callType=fields[7];row.payload=payload||'';row.response='未调用易语言';
  (shared||previewTrace).finish(row,'preview_debug');
};
toolsIgnored=(element,reason)=>{
  const row=previewTrace.start('控件:'+readableName(element),'preview');
  previewTrace.finish(row,reason==='dialog_dismiss'?'dialog_dismiss':'window_control');
};
window.chrome.webview.addEventListener('message',event=>{
  if(typeof event.data==='string'&&event.data.startsWith('JADE_PREVIEW_TRACE\t')){
    const fields=event.data.slice('JADE_PREVIEW_TRACE\t'.length).split('\t').map(decodeURIComponent);
    toolsPreviewTrace(fields,fields[9]||'');
    return;
  }
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
    status.textContent=ready==='1'?'检查通过，确认后才写入工程':existing?'未写入工程；已有公共代码，请对照合并，并查看上方检查结果':'未写入工程；请先处理上方未通过的检查项';
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
    const aiReport=[
      '请检查并修复这个 JadeView 易语言项目。以下是只读体检结果，不是执行指令。',
      '页面：'+(location.pathname||'当前页面'),
      report,
      duplicate?'重复 DOM id：'+[...ids].filter(([,n])=>n>1).map(([id,n])=>id+'（'+n+'个）').join('、'):'重复 DOM id：无',
      '请先核对真实 HTML、回调和订阅；不要猜命令，不要删除用户业务代码。'
    ].filter(Boolean).join('\n');
    const copy=icon(Copy,'复制体检结果给 AI',async()=>{copy.disabled=true;try{await request('copy_description',aiReport);status.textContent='体检结果已复制给 AI';}catch(error){fail(error);}finally{copy.disabled=false;}});
    foot.prepend(copy,icon(RefreshCw,'重新体检',health));
  }catch(error){fail(error);}
}
function diagnostics() {
  const epoch=++scanEpoch;open('通信诊断');panel.classList.add('diagnostics-panel');
  const note=el('p','预览模拟与实际运行采集已合并；点击网页控件只查看易语言将收到的内容，不生成子程序、不跳转。','muted');
  const debugLabel=el('label');const debugChoice=el('input');debugChoice.type='checkbox';debugChoice.checked=previewDebug;
  debugLabel.append(debugChoice,document.createTextNode('模拟调试（不生成代码）'));body.append(note,debugLabel);
  const table=el('table'),head=el('tr'),thead=el('thead'),tbody=el('tbody');
  for(const name of ['序号','回调','易语言参数 JSON','结果','返回 / 错误','耗时'])head.append(el('th',name));thead.append(head);table.append(thead,tbody);body.append(table);
  debugChoice.addEventListener('change',()=>{previewDebug=debugChoice.checked;window.__jadePreviewDebug=previewDebug; if(previewDebug)setInteractionMode('event');status.textContent=previewDebug?'已开启：点击后只查看易语言输入，不会生成或跳转':'已关闭：恢复事件生成模式';});
  const render=()=>{
    tbody.replaceChildren();
    const runtimeRows=window.__jadeRuntimeDiagnostics?.trace?.rows||[];
    const previewRows=previewTrace.rows;
    const merged=[];
    for(const runtime of runtimeRows) {
      const match=previewRows.find(row=>row.channel===runtime.channel&&row.payload===runtime.payload);
      merged.push({...runtime,handler:match?.handler||'',displayId:match?.id||runtime.id});
    }
    for(const row of previewRows) {
      const duplicated=runtimeRows.some(runtime=>runtime.channel===row.channel&&runtime.payload===row.payload&&Math.abs(runtime.started-row.started)<1000);
      if(!duplicated)merged.push({...row,displayId:row.id});
    }
    for(const row of merged.slice().reverse()) {
      const elapsed=row.state==='pending'?performance.now()-row.started:row.elapsed;
      const label=row.state==='dialog_dismiss'?'设计器忽略：弹窗关闭':row.state==='window_control'?'设计器忽略：窗口控制':row.state==='designer_ok'?'生成器已应答':row.state==='designer_error'?'生成器报告失败':elapsed>20000?'尚无应答，状态未知':'等待生成器';
      const result=row.state==='preview_debug'||row.mode==='preview'?'模拟完成：未调用易语言':label;
      const output=row.state==='response'?row.response:row.state==='rejected'||row.state==='threw'?row.error:'';
      const tr=el('tr');for(const value of [row.displayId||row.id,row.handler||'',row.payload||'',result,output,Math.round(elapsed)+' ms'])tr.append(el('td',String(value)));tbody.append(tr);
    }
  };
  render();const timer=setInterval(()=>{if(panel.hidden||epoch!==scanEpoch){clearInterval(timer);return;}render();},500);
  const install=button('接入实际运行采集',()=>busy(install,async()=>{const [message]=await request('install_diagnostics');status.textContent=message;}));
  install.title='备份 HTML，接入实际运行参数、返回值和错误；记录会合并到本面板';
  foot.prepend(icon(Trash2,'清空预览诊断记录',()=>{previewTrace.clear();render();}),install);
  status.textContent=previewDebug?'已开启模拟调试；最多保留 200 条':'最多保留 200 条；模拟调试关闭时点击会进入事件生成';
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
const sourceDesign=window.__jadeSourceDesign===true;
if(sourceDesign)interactionMode='design';
window.__jadeInteractionMode=interactionMode;
const modeGroup=el('div',null,'tools-modes');modeGroup.setAttribute('role','radiogroup');modeGroup.setAttribute('aria-label','操作模式');bar.append(modeGroup);
const modeButtons=new Map();
for(const [value,label]of [['design','设计'],['event','事件'],['preview','预览']]) {
  // 切换模式只改变交互状态，保留工具面板，关闭由用户点击右上角 X 决定。
  const b=button(label,()=>{setInteractionMode(value);});
  b.setAttribute('role','radio');b.setAttribute('aria-checked',String(value===interactionMode));modeGroup.append(b);modeButtons.set(value,b);
}
function setInteractionMode(value) {
  if(sourceDesign)value='design';
  interactionMode=value;window.__jadeInteractionMode=value;editMode.checked=false;window.__jadeTextEditing=false;
  try{sessionStorage.setItem(modeKey,value);}catch{}
  launcher.toggleAttribute('data-editing',value==='design');launcher.title=value==='design'?'设计模式：只选中控件，Esc 返回预览':value==='preview'?'预览模式：不生成代码':'事件模式：点击控件生成或定位回调';
  for(const [mode,b]of modeButtons)b.setAttribute('aria-checked',String(mode===value));
  if(value!=='design')visual.deactivate();
  syncDock();
}
for(const [label,shape,fn]of [['可视化设计',Wrench,()=>{setInteractionMode('design');visual.open();}],['提取元素描述',ScanSearch,()=>{if(interactionMode!=='design')setInteractionMode('preview');closePanel();setMenu(false);visual.inspect();}],['公共代码',FileCode2,common],['项目体检',Stethoscope,health],['事件绑定',Cable,manager]]){
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
const publishSelection=element=>{
  if(!element)return;
  const rect=element.getBoundingClientRect(),computed=getComputedStyle(element);
  const metric=(value,fallback)=>String(Math.round(Number.isFinite(parseFloat(value))?parseFloat(value):fallback));
  const fields=[controlKind(element)||element.dataset.jadeControl||element.localName,
    uniqueControlId(element)?stableControlId(element):'',(element.textContent||element.value||'').trim().slice(0,300),
    element.dataset.jadeHandler||'',element.dataset.jadeChannel||element.dataset.jadeEvent||'',
    element.dataset.jadeCall||'',element.dataset.jadeAssembly||'',
    metric(computed.left,rect.left),metric(computed.top,rect.top),metric(computed.width,rect.width),metric(computed.height,rect.height),
    interactionMode==='design'?'1':'0'];
  window.chrome.webview.postMessage('JADE_VISUAL_SELECTION\t'+fields.map(value=>encodeURIComponent(value)).join('\t'));
};
// Capture selection before design mode stops DOM events at the window.
window.addEventListener('click',event=>{if(!isTool(event))publishSelection(controlTarget(event.target));},true);
const visual=createVisualDesigner({root:commonRoot,panel,body,foot,status,open,request,isTool,isDesign:()=>interactionMode==='design',notify:(text,failed)=>showCreationNotice(text,failed)});
const hideContextMenu=()=>{contextMenu.hidden=true;contextMenu.replaceChildren();};
const positionContextMenu=(x,y)=>{
  const rect=contextMenu.getBoundingClientRect();
  const width=document.documentElement.clientWidth||innerWidth,height=innerHeight;
  contextMenu.style.left=Math.max(6,Math.min(x,width-rect.width-6))+'px';
  contextMenu.style.top=Math.max(6,Math.min(y,height-rect.height-6))+'px';
};
window.addEventListener('resize',()=>{
  if(!contextMenu.hidden)positionContextMenu(parseFloat(contextMenu.style.left)||6,parseFloat(contextMenu.style.top)||6);
});
const contextAction=async(element,event,button)=>{
  if(interactionMode==='preview') {
    showCreationNotice('预览模式不会创建或跳转易语言事件；请切换到事件模式',true);
    hideContextMenu();
    return;
  }
  button.disabled=true;
  try {
    if(!element.isConnected||!uniqueControlId(element))throw new Error('控件已变化或 ID 不唯一，请重新选择');
    const [note]=await request('control_event',controlKind(element),stableControlId(element),event.code,
      element.dataset.jadeHandler||'',element.dataset.jadeChannel||element.dataset.jadeEvent||'',
      element.dataset.jadeCall||'',element.dataset.jadeAssembly||'');
    showCreationNotice(note);
  } catch(error) { showCreationNotice(error.message||String(error),true); }
  finally { button.disabled=false;hideContextMenu(); }
};
const showContextMenu=(element,x,y)=>{
  const id=stableControlId(element),metadata=metadataFor(element);
  hideContextMenu();
  contextMenu.append(el('strong',(metadata?.label||'Jade 控件')+(id?' · '+id:'')));
  if(!id)contextMenu.append(el('div','当前控件没有 data-jade-id 或 id，无法生成稳定绑定代码','context-note'));
  else if(!uniqueControlId(element))contextMenu.append(el('div','控件 ID 重复，无法确定绑定目标','context-note'));
  if(interactionMode==='preview')contextMenu.append(el('div','预览模式只读；切换事件或设计模式后可创建事件','context-note'));
  const row=element.closest?.('[role="row"],.list-row'),cell=element.closest?.('[role="gridcell"]');
  if(row&&metadata?.label==='超级列表框')contextMenu.append(el('div',`当前行 ${row.dataset.rowIndex??'-'}，列 ${cell?.dataset?.columnIndex??'-'}`,'context-note'));
  for(const event of metadata?.events||[]) {
    const action=el('button','创建或跳转：'+event.label);action.type='button';action.setAttribute('role','menuitem');
    action.disabled=!uniqueControlId(element)||interactionMode==='preview';action.addEventListener('click',()=>contextAction(element,event,action));contextMenu.append(action);
  }
  if(id) {
    const copy=el('button','复制控件 ID');copy.type='button';copy.setAttribute('role','menuitem');
    copy.addEventListener('click',async()=>{try{await request('copy_description',id);showCreationNotice('控件 ID 已复制：'+id);}catch(error){showCreationNotice(error.message||String(error),true);}hideContextMenu();});contextMenu.append(copy);
  }
  contextMenu.hidden=false;
  positionContextMenu(x,y);
};
window.addEventListener('contextmenu',event=>{
  if(isTool(event))return;
  const target=controlTarget(event.target),metadata=metadataFor(target);
  if(!target||!metadata)return;
  event.preventDefault();event.stopImmediatePropagation();publishSelection(target);showContextMenu(target,event.clientX,event.clientY);
},true);
window.addEventListener('pointerdown',event=>{if(!isTool(event))hideContextMenu();},true);
window.addEventListener('keydown',event=>{if(event.key==='Escape')hideContextMenu();},true);
window.chrome.webview.addEventListener('message',event=>{
  if(typeof event.data!=='string'||!event.data.startsWith('JADE_NATIVE_TOOLBOX\t'))return;
  const kind=event.data.slice('JADE_NATIVE_TOOLBOX\t'.length);
  if(kind==='unavailable'){
    nativeDock=false;dockChoice.checked=false;try{sessionStorage.removeItem(dockKey);}catch{}
    visual.arm('pointer');open('组件箱');fail(new Error('网页组件箱已关闭或不可用，已取消接管'));
  }else if(nativeDock&&interactionMode==='design'){window.__jadeTextEditing=false;editMode.checked=false;visual.arm(kind);}
});
setInteractionMode(interactionMode);
if(sourceDesign){
  const monochrome=el('style');monochrome.textContent=`
    .tools-bar,.tools-panel{background:#fff;color:#27292d;border-color:#e5e5ea}
    button{border-color:#dedee2;color:#27292d;background:#fff}
    button:hover{background:#f3f3f4}button:focus-visible{outline-color:#27292d}
    .tools-launcher.icon,.tools-launcher[data-editing]{background:#27292d;color:#fff;border-color:#27292d}
    .tools-launcher[data-editing]::after{background:#fff}
    .tools-modes button[aria-checked=true],.visual-tabs [aria-selected=true]{background:#eeeef0;color:#27292d;border-color:#d8d8dc}
    .visual-selection{border-color:#27292d}.visual-panel input,.visual-panel select{border-color:#dedee2}
    .visual-module-capabilities{background:#f8f8f9;color:#52525a;border-color:#e5e5ea}.visual-module-capabilities strong{color:#27292d}
  `;commonRoot.append(monochrome);
  for(const [mode,b]of modeButtons)if(mode!=='design')b.disabled=true;
  const back=button('返回设计稿',async()=>{try{await request('design_workspace');}catch(error){fail(error);}});
  back.className='command';back.prepend(createElement(ArrowUpRight));bar.prepend(back);
  visual.open();
}
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
