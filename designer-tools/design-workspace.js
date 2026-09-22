import {createIcons, MousePointer2, Hand, Square, Type, TextCursorInput, ChevronDown, Table2, ListTree, CheckSquare, CircleDot, PanelsTopLeft, Gauge, SlidersHorizontal, Hash, Undo2, Redo2, Save, Download, Upload, Copy, Trash2, FilePlus2, Layers, Palette, Shapes, Sparkles, Plus, Minus, Maximize, Eye, EyeOff, Lock, LockOpen, ArrowUp, ArrowDown, Group, Ungroup, AlignLeft, AlignCenter, AlignRight, Search, Check, Settings, Play, User, Star, PanelLeftClose, LayoutTemplate} from 'lucide';
import spec from '../docs/AI生成UI与JadeView支持库对接规范.md';
import css from './design-workspace.css';
import {House,Heart,Menu,Bell,Folder,ChartColumn,X} from 'lucide';
import componentCss from './design-components.css';
import shellCss from './design-workspace-shell.css';
import themeCss from './jade-ui-theme.css';
import {createSourceCanvas} from './design-source-canvas.js';
import {PRESETS,COMPONENT_GROUPS,RESIZE_DIRECTIONS,resizeGeometry,NAV_ICONS,isNavigation,navigationOf} from './design-components.js';
import {TYPES,clone,newDesign,newNode,newComponent,validate,importControls,buildRequest,sourceIds,themeOf,styleOf,moveLayer,duplicateNodes,upgradeDefaultTheme} from './design-model.js';

const icons={MousePointer2,Hand,Square,Type,TextCursorInput,ChevronDown,Table2,ListTree,CheckSquare,CircleDot,PanelsTopLeft,Gauge,SlidersHorizontal,Hash,Undo2,Redo2,Save,Download,Upload,Copy,Trash2,FilePlus2,Layers,Palette,Shapes,Sparkles,Plus,Minus,Maximize,Eye,EyeOff,Lock,LockOpen,ArrowUp,ArrowDown,Group,Ungroup,AlignLeft,AlignCenter,AlignRight,Search,Check,Settings,Play,User,Star,PanelLeftClose,LayoutTemplate};
const kindIcons={button:'square',edit:'text-cursor-input',select:'chevron-down','super-list':'table-2',tree:'list-tree',checkbox:'check-square',radio:'circle-dot',tabs:'panels-top-left',progress:'gauge',slider:'sliders-horizontal',number:'hash',label:'type',container:'square'};
Object.assign(icons,{House,Heart,Menu,Bell,Folder,ChartColumn,X});
const sections={components:'组件',layers:'图层',colors:'配色',shapes:'形状',fonts:'字体',motion:'动效',ai:'AI 对接'};
const palettes=[['黑白','#27292d'],['石墨','#454b4c'],['玫红','#bd4266'],['朱红','#be4a38'],['深棕','#796453'],['草绿','#557b37']];
const style=document.createElement('style');style.textContent=css+'\n'+componentCss+'\n'+themeCss+'\n'+shellCss;document.head.append(style);
document.body.innerHTML=`<header><div class="brand"><i data-lucide="layers"></i><strong>Jade设计</strong></div><input id="title" aria-label="界面名称" maxlength="120"><span class="document-state" id="status" role="status">读取设计稿…</span><nav id="tools"></nav></header>
<main><nav id="rail" aria-label="设计面板"></nav><aside id="library"><div class="panel-heading"><h2 id="panel-title">组件</h2><span id="panel-count"></span></div><div id="panel-body"></div></aside>
<section id="viewport" aria-label="设计画布"><div id="stage"><div id="artboard-label"><i data-lucide="panels-top-left"></i><span id="artboard-name"></span><span id="artboard-size"></span></div><div id="canvas"></div></div><div id="canvas-tools"></div><div id="zoom-tools"></div></section>
<aside id="inspector"><div class="panel-heading"><h2 id="inspector-title">画布</h2><span id="selection-count"></span></div><div id="property-tabs"></div><div id="fields"></div></aside></main>
<footer><span id="project"></span><span>设计稿 · 待 AI 实现</span></footer>
<dialog id="export"><form method="dialog"><h2>AI 开发任务</h2><button value="close">关闭</button></form><textarea aria-label="AI 开发任务" readonly></textarea><button id="copy">复制完整任务</button><button id="download">下载任务文件</button></dialog><input id="file" type="file" accept=".json,application/json" hidden>`;
const $=s=>document.querySelector(s),status=text=>$('#status').textContent=text;
let doc=newDesign(),selected='',chosen=new Set(),undo=[],redo=[],sourceHtml='',reservedIds=[],ready=false,saving=false,serial=0,projectKey='',lastSaved='',pending=new Map();
let panel='components',propertyTab='design',search='',mode='select',space=false,snap=true;
const sourceCanvas=createSourceCanvas();
let sourceLoading=false;
const camera={x:40,y:65,zoom:1};
const bridge=window.chrome?.webview;
function native(action,...values){
  if(!bridge)return Promise.reject(Error('当前未连接易语言支持库'));
  return new Promise((resolve,reject)=>{const id=String(++serial),timer=setTimeout(()=>{pending.delete(id);reject(Error('支持库响应超时，请重试'));},10000);pending.set(id,{resolve,reject,timer});bridge.postMessage(['JADE_TOOL',id,action,window.__jadeDesignToken||'',...values].map(encodeURIComponent).join('\t'));});
}
bridge?.addEventListener('message',event=>{if(typeof event.data!=='string'||!event.data.startsWith('JADE_TOOL_RESULT\t'))return;const [,id,ok,...fields]=event.data.split('\t').map(decodeURIComponent),p=pending.get(id);if(!p)return;clearTimeout(p.timer);pending.delete(id);ok==='1'?p.resolve(fields):p.reject(Error(fields[0]||'操作失败'));});
function run(fn){if(ready)Promise.resolve().then(fn).catch(e=>status(e.message));}
function iconify(){createIcons({icons,attrs:{width:18,height:18}});}
function button(parent,label,icon,fn,caption=''){
  const b=document.createElement('button');b.type='button';b.title=label;b.setAttribute('aria-label',label);
  if(icon)b.innerHTML=`<i data-lucide="${icon}"></i>`;
  if(caption){const s=document.createElement('span');s.textContent=caption;b.append(s);}b.onclick=()=>run(fn);parent.append(b);return b;
}
function heading(parent,text){const h=document.createElement('h3');h.textContent=text;parent.append(h);}
function remember(){undo.push(clone(doc));if(undo.length>60)undo.shift();redo=[];}
function stash(){const json=JSON.stringify(doc),value=json===lastSaved?'':json;if(bridge){bridge.postMessage('JADE_DESIGN_DRAFT\t'+window.__jadeDesignToken+'\t'+encodeURIComponent(value));return;}try{if(value)localStorage.setItem(projectKey,value);else localStorage.removeItem(projectKey);}catch{status('本地草稿保存失败，请导出设计稿');}}
function changed(){stash();status(JSON.stringify(doc)===lastSaved?'已保存 · 待实现':'未保存');render();}
function edit(fn){if(!ready)return;const next=clone(doc);fn(next);validate(next);remember();doc=next;changed();}
function field(parent,label,value,apply,type='text',min,max){
  const wrap=document.createElement('label');wrap.textContent=label;const input=document.createElement(type==='textarea'?'textarea':'input');
  if(type!=='textarea')input.type=type;input.value=value;input.setAttribute('aria-label',label);if(min!==undefined)input.min=min;if(max!==undefined)input.max=max;
  input.onchange=()=>{try{apply(type==='number'||type==='range'?Number(input.value):input.value);}catch(e){status(e.message);input.value=value;}};wrap.append(input);parent.append(wrap);return input;
}
function choices(parent,label,items,value,apply){
  const box=document.createElement('div');box.className='choices';box.setAttribute('role','group');box.setAttribute('aria-label',label);
  for(const [id,text,icon] of items){const b=button(box,text,icon,()=>apply(id),icon?'':text);b.setAttribute('aria-pressed',String(id===value));}parent.append(box);return box;
}
function toggle(parent,label,value,apply){const wrap=document.createElement('label');wrap.className='toggle';const i=document.createElement('input');i.type='checkbox';i.checked=value;i.onchange=()=>run(()=>apply(i.checked));const s=document.createElement('span');s.textContent=label;wrap.append(i,s);parent.append(wrap);}
const selectedNode=()=>doc.nodes.find(n=>n.id===selected);
function setStyle(key,value){edit(d=>{for(const n of d.nodes)if(chosen.has(n.id))n.style={...styleOf(n),[key]:value};});}
function setTheme(key,value){edit(d=>d.theme={...themeOf(d),[key]:value});}
function select(n,multiple=false,single=false){
  if(!multiple)chosen.clear();const group=styleOf(n).group;
  const ids=group&&!single?doc.nodes.filter(x=>styleOf(x).group===group).map(x=>x.id):[n.id];
  if(multiple&&chosen.has(n.id))ids.forEach(id=>chosen.delete(id));else ids.forEach(id=>chosen.add(id));
  selected=chosen.has(n.id)?n.id:[...chosen].at(-1)||'';render();$('#inspector').scrollTop=0;
}
function applyCamera(){
  $('#stage').style.transform=`translate(${camera.x}px,${camera.y}px) scale(${camera.zoom})`;
  $('#canvas').style.setProperty('--handle-size',8/camera.zoom+'px');
  $('#canvas').style.setProperty('--edge-size',8/camera.zoom+'px');
  $('#zoom-value').textContent=Math.round(camera.zoom*100)+'%';$('#viewport').classList.toggle('hand',mode==='hand'||space);
}
function zoomTo(z,anchor){
  const rect=$('#viewport').getBoundingClientRect(),x=anchor?.x??rect.width/2,y=anchor?.y??rect.height/2,next=Math.min(2,Math.max(.15,z));
  camera.x=x-(x-camera.x)*next/camera.zoom;camera.y=y-(y-camera.y)*next/camera.zoom;camera.zoom=next;applyCamera();
}
function fit(){const r=$('#viewport').getBoundingClientRect();camera.zoom=Math.max(.15,Math.min(1,(r.width-64)/doc.width,(r.height-140)/doc.height));camera.x=(r.width-doc.width*camera.zoom)/2;camera.y=80+Math.max(0,(r.height-150-doc.height*camera.zoom)/2);applyCamera();}
function nodeView(n,mini=false){
  const view=document.createElement('div'),s=styleOf(n),t=themeOf(doc);view.className='node-view jade-ui-control kind-'+n.kind+' variant-'+s.variant;
  if(n.preset)view.classList.add('preset-'+n.preset);
  Object.assign(view.style,{borderRadius:(s.radius??t.radius)+'px',fontFamily:t.font,fontSize:(s.fontSize??t.fontSize)+'px',textAlign:s.align,transitionDuration:t.motion+'ms'});
  if(s.background)view.style.background=s.background;if(s.color)view.style.color=s.color;
  const span=(text,cls='')=>{const e=document.createElement('span');e.textContent=text;e.className=cls;view.append(e);return e;};
  if(isNavigation(n)){
    const nav=navigationOf(n),rail=n.preset==='navigation-rail';
    view.setAttribute('aria-label',n.text);
    const top=span('','nav-top');
    if(nav.menu){const icon=document.createElement('i');icon.dataset.lucide='menu';top.append(icon);}
    if(!rail){const title=document.createElement('span');title.textContent=n.text;top.append(title);}
    const items=span('','nav-items');
    for(const item of nav.items){
      const entry=document.createElement('span');entry.className='nav-item'+(item.id===nav.selectedId?' active':'');entry.dataset.itemId=item.id;
      const pill=document.createElement('span');pill.className='nav-icon';const icon=document.createElement('i');icon.dataset.lucide=item.icon==='chart'?'chart-column':item.icon;pill.append(icon);
      const label=document.createElement('span');label.className='nav-label';label.textContent=item.text;entry.append(pill,label);items.append(entry);
    }
  }
  else if(n.preset==='switch'){span('','switch-track');span(n.text);}
  else if(['card','toolbar','dialog'].includes(n.preset)){
    const title=span(n.text,'layout-title');
    if(n.preset==='dialog'){const close=document.createElement('i');close.dataset.lucide='x';title.append(close);}
    if(n.preset==='toolbar'){for(const name of ['plus','search','settings']){const i=document.createElement('i');i.dataset.lucide=name;view.append(i);}}
    else {
      const body=span('','layout-body');
      for(const label of ['名称','备注']){const row=document.createElement('span');row.className='layout-field';const text=document.createElement('span');text.textContent=label;const input=document.createElement('span');input.className='layout-input';row.append(text,input);body.append(row);}
      if(n.preset==='dialog'){const actions=span('','dialog-actions');for(const text of ['取消','确认']){const b=document.createElement('span');b.textContent=text;actions.append(b);}}
    }
  }
  else if(['divider','rectangle','ellipse'].includes(n.preset)){span('','shape-fill');}
  else if(n.kind==='super-list'){
    const head=document.createElement('div');head.className='grid-head';head.style.gridTemplateColumns=n.columns.map(c=>c.width+'px').join(' ');
    for(const c of n.columns){const h=document.createElement('span');h.textContent=c.title;head.append(h);}view.append(head);span(mini?'':n.text,'grid-empty').style.backgroundSize=`100% ${n.rowHeight}px`;
  }else if(n.kind==='checkbox'||n.kind==='radio'){span('',n.kind==='radio'?'radio-mark':'check-mark');span(n.text);}
  else if(n.kind==='progress'){const p=document.createElement('progress');p.value=45;p.max=100;view.append(p);}
  else if(n.kind==='slider'){const p=document.createElement('input');p.type='range';p.tabIndex=-1;view.append(p);}
  else if(n.kind==='tree'){for(const [text,child] of [[n.text,false],['子节点',true]]){const row=span('','tree-row'+(child?' tree-child':''));const i=document.createElement('i');i.dataset.lucide=child?'folder':'chevron-down';const label=document.createElement('span');label.textContent=text;row.append(i,label);}}
  else if(n.kind==='tabs'){span(n.text,'tab-active');span('选项页 2');}
  else {if(s.icon){const i=document.createElement('i');i.dataset.lucide=s.icon;view.append(i);}span(n.kind==='edit'?(n.placeholder||n.text):n.text,'node-label');if(n.kind==='select'||n.kind==='number'){const affix=span('','field-affix');for(const name of n.kind==='select'?['chevron-down']:['minus','plus']){const i=document.createElement('i');i.dataset.lucide=name;affix.append(i);}}}
  if(n.kind==='button'||n.kind==='label')view.style.justifyContent=({left:'flex-start',center:'center',right:'flex-end'})[s.align];
  return view;
}
function startGesture(e,n,resize=''){
  if(e.button!==0||mode==='hand'||space)return;
  e.stopPropagation();e.preventDefault();
  if(!chosen.has(n.id)||e.shiftKey)select(n,e.shiftKey,e.altKey);else {selected=n.id;render();}
  if(styleOf(n).locked)return;
  const el=$('#canvas').querySelector(`[data-node-id="${CSS.escape(n.id)}"]`);if(!el)return;
  el.setPointerCapture(e.pointerId);
  const before=clone(doc),previousRedo=redo.slice(),sx=e.clientX,sy=e.clientY,targets=doc.nodes.filter(x=>chosen.has(x.id)&&!styleOf(x).locked);let moved=false;
  const origin=before.nodes.find(x=>x.id===n.id);
  const grid=v=>snap&&!e.ctrlKey?Math.round(v/4)*4:v;
  el.onpointermove=p=>{
    if(!moved&&Math.abs(p.clientX-sx)+Math.abs(p.clientY-sy)>3){remember();moved=true;}if(!moved)return;
    let dx=grid((p.clientX-sx)/camera.zoom),dy=grid((p.clientY-sy)/camera.zoom);
    if(resize){
      const current=doc.nodes.find(x=>x.id===n.id);Object.assign(current,resizeGeometry(origin,resize,dx,dy,doc));
      Object.assign(el.style,{left:current.x+'px',top:current.y+'px',width:current.width+'px',height:current.height+'px'});
      for(const [key,label] of [['x','左边'],['y','顶边'],['width','宽度'],['height','高度']]){const input=$(`#fields input[aria-label="${label}"]`);if(input)input.value=current[key];}
    }
    else {
      const original=before.nodes.filter(x=>targets.some(t=>t.id===x.id));
      dx=Math.max(-Math.min(...original.map(x=>x.x)),Math.min(doc.width-Math.max(...original.map(x=>x.x+x.width)),dx));
      dy=Math.max(-Math.min(...original.map(x=>x.y)),Math.min(doc.height-Math.max(...original.map(x=>x.y+x.height)),dy));
      for(const old of original){const current=doc.nodes.find(x=>x.id===old.id);current.x=Math.max(0,old.x+dx);current.y=Math.max(0,old.y+dy);const node=$('#canvas').querySelector(`[data-node-id="${CSS.escape(old.id)}"]`);if(node){node.style.left=current.x+'px';node.style.top=current.y+'px';}}
    }
    if(doc.sourcePage){sourceCanvas.update(doc);for(const item of targets.filter(x=>x.source)){const r=sourceCanvas.bounds(item.id),node=$('#canvas').querySelector(`[data-node-id="${CSS.escape(item.id)}"]`);if(r&&node)Object.assign(node.style,{left:r.x+'px',top:r.y+'px',width:r.width+'px',height:r.height+'px'});}}
  };
  const finish=cancel=>{el.onpointermove=null;if(moved){if(cancel){doc=before;undo.pop();redo=previousRedo;}changed();}else render();};
  el.onpointerup=()=>finish(false);el.onpointercancel=()=>finish(true);
}
function renderCanvas(){
  const canvas=$('#canvas'),t=themeOf(doc);
  for(const child of [...canvas.children])if(child!==sourceCanvas.frame)child.remove();
  if(!doc.sourcePage&&!sourceLoading)sourceCanvas.clear();
  canvas.style.width=doc.width+'px';canvas.style.height=doc.height+'px';
  const vars={'--primary':t.primary,'--paper':t.surface,'--ink':t.text,'--line':t.border,'--page':t.background};
  for(const [key,value] of Object.entries(vars))document.documentElement.style.setProperty(key,value);
  if(doc.sourcePage&&sourceCanvas.ready){
    sourceCanvas.update(doc);
    const hit=document.createElement('div');hit.className='source-canvas-hit';hit.onpointerdown=pickSource;
    hit.addEventListener('wheel',e=>{if(e.ctrlKey)return;e.preventDefault();e.stopPropagation();const p=sourcePoint(e);sourceCanvas.scroll(p.x,p.y,e.deltaX,e.deltaY);render();},{passive:false});canvas.append(hit);
  }
  for(const n of doc.nodes){const s=styleOf(n);if(s.hidden)continue;const root=document.createElement('div');root.className='node'+(chosen.has(n.id)?' selected':'')+(s.locked?' locked':'');root.dataset.nodeId=n.id;
    if(n.source&&!chosen.has(n.id))continue;
    const bounds=n.source?sourceCanvas.bounds(n.id):n;if(!bounds)continue;
    Object.assign(root.style,{left:bounds.x+'px',top:bounds.y+'px',width:bounds.width+'px',height:bounds.height+'px'});
    if(!n.source)root.append(nodeView(n));root.onpointerdown=e=>n.source?pickSource(e):startGesture(e,n);
    root.ondblclick=()=>{const input=$('#fields input[aria-label="显示文字"]');input?.focus();input?.select();};
    if(chosen.has(n.id)&&!s.locked){for(const [direction,label] of Object.entries(RESIZE_DIRECTIONS)){
      const handle=document.createElement('div');handle.className='resize-handle';handle.dataset.direction=direction;handle.title=label+'调整尺寸';handle.onpointerdown=e=>startGesture(e,n,direction);root.append(handle);
    }}canvas.append(root);
  }
  $('#artboard-name').textContent=doc.title;$('#artboard-size').textContent=`${doc.width} × ${doc.height}`;applyCamera();
}
function sourcePoint(e){const r=$('#canvas').getBoundingClientRect();return {x:(e.clientX-r.left)/camera.zoom,y:(e.clientY-r.top)/camera.zoom};}
function pickSource(e){if(e.button!==0||mode==='hand'||space)return;const p=sourcePoint(e),id=sourceCanvas.hit(p.x,p.y),n=doc.nodes.find(n=>n.source&&n.id===id);if(n)startGesture(e,n);}
function renderPalette(body){
  const input=document.createElement('input');input.className='search';input.placeholder='搜索组件';input.setAttribute('aria-label','搜索组件');input.value=search;input.oninput=()=>{search=input.value;renderComponentList(list);};body.append(input);
  const list=document.createElement('div');list.id='palette';body.append(list);renderComponentList(list);
}
function renderComponentList(list){
  list.replaceChildren();
  for(const [title,kinds] of COMPONENT_GROUPS){
    const label=k=>PRESETS[k]?.label||TYPES[k];
    const found=kinds.filter(k=>label(k).includes(search)||k.includes(search.toLowerCase()));if(!found.length)continue;heading(list,title);const grid=document.createElement('div');grid.className='component-grid';list.append(grid);
    for(const kind of found){const b=document.createElement('button');b.className='component';b.setAttribute('aria-label',label(kind));b.title='添加'+label(kind);
      const thumb=document.createElement('div');thumb.className='component-thumb';const n=newComponent(kind,[]);n.style={...n.style,align:'center'};if(kind==='super-list')n.columns=[{title:'名称',width:56},{title:'内容',width:56}];thumb.append(nodeView(n,true));
      const caption=document.createElement('span');caption.textContent=label(kind);b.append(thumb,caption);b.onclick=()=>run(()=>add(kind));b.draggable=true;b.ondragstart=e=>e.dataTransfer.setData('text/jade-kind',kind);grid.append(b);
    }
  }iconify();
}
function renderLayers(body){
  const actions=document.createElement('div');actions.className='layer-actions';body.append(actions);
  button(actions,'复制控件','copy',duplicate);button(actions,'组合','group',group).disabled=chosen.size<2;button(actions,'取消组合','ungroup',()=>setStyle('group','')).disabled=![...chosen].some(id=>styleOf(doc.nodes.find(n=>n.id===id)).group);
  button(actions,'上一层','arrow-up',()=>edit(d=>moveLayer(d,selected,1))).disabled=!selected;button(actions,'下一层','arrow-down',()=>edit(d=>moveLayer(d,selected,-1))).disabled=!selected;
  const list=document.createElement('div');list.id='layers';body.append(list);
  for(const n of [...doc.nodes].reverse()){
    const s=styleOf(n),row=document.createElement('div');row.className='layer-row'+(chosen.has(n.id)?' active':'');row.draggable=true;row.dataset.layerId=n.id;
    const choose=button(row,n.text+' · '+n.id,s.group?'group':kindIcons[n.kind],()=>select(n),n.text);choose.className='layer-name';choose.onclick=e=>select(n,e.shiftKey,e.altKey);
    button(row,(s.hidden?'显示 ':'隐藏 ')+n.id,s.hidden?'eye-off':'eye',()=>edit(d=>{const node=d.nodes.find(x=>x.id===n.id);node.style={...styleOf(node),hidden:!s.hidden};}));
    button(row,(s.locked?'解锁 ':'锁定 ')+n.id,s.locked?'lock':'lock-open',()=>edit(d=>{const node=d.nodes.find(x=>x.id===n.id);node.style={...styleOf(node),locked:!s.locked};}));
    row.ondragstart=e=>e.dataTransfer.setData('text/jade-layer',n.id);row.ondragover=e=>e.preventDefault();row.ondrop=e=>{e.preventDefault();const id=e.dataTransfer.getData('text/jade-layer');if(id)edit(d=>moveLayer(d,id,d.nodes.findIndex(x=>x.id===n.id)-d.nodes.findIndex(x=>x.id===id)));};list.append(row);
  }
}
function renderPanel(){
  $('#panel-title').textContent=sections[panel];$('#panel-count').textContent=panel==='layers'?doc.nodes.length+' 个':panel==='components'?COMPONENT_GROUPS.flatMap(([,items])=>items).length+' 项':'';
  for(const b of $('#rail').children)b.setAttribute('aria-pressed',String(b.dataset.panel===panel));
  const body=$('#panel-body');body.replaceChildren();const t=themeOf(doc);
  if(panel==='components')renderPalette(body);
  if(panel==='layers')renderLayers(body);
  if(panel==='colors'){
    heading(body,'主题色');const swatches=document.createElement('div');swatches.className='swatches';body.append(swatches);
    for(const [name,color] of palettes){const b=button(swatches,name,'check',()=>setTheme('primary',color));b.style.background=color;b.classList.toggle('current',color===t.primary);}
    heading(body,'明暗');choices(body,'明暗',[['light','浅色'],['dark','深色']],t.background==='#202429'?'dark':'light',value=>edit(d=>{d.theme={...themeOf(d),...(value==='dark'?{background:'#202429',surface:'#2c3238',text:'#edf2f5',border:'#4a555f'}:{background:'#f7f9fa',surface:'#ffffff',text:'#24313b',border:'#d6dfe4'})};}));
    heading(body,'颜色角色');for(const [key,name] of [['primary','强调色'],['background','页面背景'],['surface','控件背景'],['text','文字颜色'],['border','边框颜色']])field(body,name,t[key],v=>setTheme(key,v),'color');
  }
  if(panel==='shapes'){
    heading(body,'全局圆角');choices(body,'全局形状',[[0,'直角'],[8,'圆角'],[24,'柔和'],[100,'全圆']],t.radius,v=>setTheme('radius',v));
    field(body,'圆角半径',t.radius,v=>setTheme('radius',v),'range',0,100);
    const sample=document.createElement('div');sample.className='shape-samples';for(const r of [0,8,24,100]){const b=button(sample,'圆角 '+r,'square',()=>setTheme('radius',r));b.style.borderRadius=r+'px';b.style.background=t.primary;}body.append(sample);
    if(selectedNode())button(body,'选中控件跟随主题','undo-2',()=>setStyle('radius',null),'选中控件跟随主题');
  }
  if(panel==='fonts'){
    heading(body,'字体');choices(body,'字体',[['Microsoft YaHei UI','微软雅黑'],['Segoe UI','系统'],['SimSun','宋体']],t.font,v=>setTheme('font',v));
    field(body,'全局字号',t.fontSize,v=>setTheme('fontSize',v),'number',12,36);
    const preview=document.createElement('div');preview.className='type-sample';preview.style.fontFamily=t.font;preview.style.fontSize=t.fontSize+'px';preview.textContent='界面设计\nJadeView 0123456789';body.append(preview);
  }
  if(panel==='motion'){
    heading(body,'颜色与样式过渡');choices(body,'过渡时长',[[0,'关闭'],[150,'轻快'],[250,'柔和']],t.motion,v=>setTheme('motion',v));
    const sample=button(body,'播放动效','play',()=>{sample.animate([{transform:'scale(.9)',opacity:.4},{transform:'scale(1)',opacity:1}],{duration:t.motion||1});},'播放');sample.className='motion-sample';
  }
  if(panel==='ai'){
    field(body,'界面需求',doc.brief,v=>edit(d=>d.brief=v),'textarea');button(body,'生成 AI 开发任务','sparkles',exportTask,'生成 AI 开发任务');
    button(body,'编辑现有网页','panels-top-left',editSource,'编辑现有网页');
    button(body,'仅导入控件身份','copy',importCurrent,'仅导入控件身份');
    const note=document.createElement('p');note.className='contract-status';note.textContent='对接规范：已内置\n运行联调：待验证';body.append(note);
  }
}
function renderProperties(){
  const fields=$('#fields');fields.replaceChildren();const n=selectedNode(),tabs=$('#property-tabs');tabs.replaceChildren();$('#inspector-title').textContent=n?(PRESETS[n.preset]?.label||TYPES[n.kind]):'画布';$('#selection-count').textContent=chosen.size>1?chosen.size+' 个已选':'';
  if(!n){
    heading(fields,'画板尺寸');choices(fields,'画板预设',[['desktop','桌面'],['compact','紧凑'],['phone','手机']],'',preset=>{edit(d=>{[d.width,d.height]=preset==='desktop'?[1100,760]:preset==='compact'?[800,600]:[390,844];});fit();});
    field(fields,'画布宽度',doc.width,v=>edit(d=>d.width=v),'number',320,4000);field(fields,'画布高度',doc.height,v=>edit(d=>d.height=v),'number',240,4000);
    field(fields,'界面需求',doc.brief,v=>edit(d=>d.brief=v),'textarea');return;
  }
  choices(tabs,'属性分类',[['design','设计'],['binding','对接']],propertyTab,v=>{propertyTab=v;renderProperties();iconify();});
  const id=field(fields,'控件 ID',n.id,()=>{});id.readOnly=true;const s=styleOf(n),t=themeOf(doc);
  const set=(key,value)=>edit(d=>d.nodes.find(x=>x.id===n.id)[key]=value);
  if(propertyTab==='binding'){
    if(n.kind==='button'){field(fields,'中文回调',n.handler,v=>set('handler',v));field(fields,'通讯通道',n.channel,v=>set('channel',v));}
    field(fields,'行为与样式要求',n.note,v=>set('note',v),'textarea');
    const p=document.createElement('p');p.className='contract-status';p.textContent=n.kind==='super-list'?'易语言绑定：Jade超级列表框绑定\n表项操作与列表事件按完整规范实现。':n.kind==='button'?'业务按钮需真实 jade.invoke 调用。':'事件接入范围以模块对接规范为准。';fields.append(p);return;
  }
  heading(fields,'内容');const textInput=field(fields,'显示文字',n.text,v=>set('text',v));if(n.source&&!sourceCanvas.canEditText(n.id)){textInput.readOnly=true;textInput.title='复杂或动态内容，交给 AI 修改原网页';}if(n.kind==='edit')field(fields,'提示文本',n.placeholder,v=>set('placeholder',v));
  if(isNavigation(n)){
    const nav=navigationOf(n);
    const update=fn=>edit(d=>{const node=d.nodes.find(x=>x.id===n.id);node.navigation=clone(navigationOf(node));fn(node.navigation);});
    toggle(fields,'显示菜单图标',nav.menu,v=>update(x=>x.menu=v));
    heading(fields,'导航项');
    nav.items.forEach((item,index)=>{
      const row=document.createElement('div');row.className='nav-item-fields';
      field(row,`第 ${index+1} 项文字`,item.text,v=>update(x=>x.items[index].text=v));
      const select=document.createElement('select');select.setAttribute('aria-label',`第 ${index+1} 项图标`);
      for(const [id,label] of Object.entries(NAV_ICONS))select.add(new Option(label,id));select.value=item.icon;
      select.onchange=()=>run(()=>update(x=>x.items[index].icon=select.value));row.append(select);
      const current=document.createElement('input');current.type='radio';current.name='navigation-current';current.checked=nav.selectedId===item.id;current.setAttribute('aria-label',`选中第 ${index+1} 项`);current.title='默认选中';current.onchange=()=>run(()=>update(x=>x.selectedId=item.id));row.append(current);
      button(row,`删除导航第 ${index+1} 项`,'trash-2',()=>update(x=>{x.items.splice(index,1);if(x.selectedId===item.id)x.selectedId=x.items[0].id;})).disabled=nav.items.length===1;
      fields.append(row);
    });
    button(fields,'添加导航项','plus',()=>update(x=>{let i=1;while(x.items.some(item=>item.id==='item-'+i))i++;x.items.push({id:'item-'+i,text:'新页面',icon:'house'});}),'添加导航项').disabled=nav.items.length>=12;
  }
  heading(fields,'布局');const geometry=document.createElement('div');geometry.className='geometry';fields.append(geometry);for(const [key,name] of [['x','左边'],['y','顶边'],['width','宽度'],['height','高度']])field(geometry,name,n[key],v=>set(key,v),'number',key==='x'||key==='y'?0:24,4000);
  choices(fields,'文字对齐',[['left','左对齐','align-left'],['center','居中','align-center'],['right','右对齐','align-right']],s.align,v=>setStyle('align',v));
  heading(fields,'样式');if(n.kind==='button'){
    choices(fields,'按钮样式',[['filled','填充'],['tonal','柔色'],['outline','描边'],['text','文字']],s.variant,v=>setStyle('variant',v));
    const select=document.createElement('select');select.setAttribute('aria-label','按钮图标');for(const [value,text] of [['','无图标'],['plus','添加'],['search','搜索'],['check','确认'],['download','下载'],['settings','设置'],['play','开始'],['user','用户'],['star','收藏']]){const option=new Option(text,value);select.add(option);}select.value=s.icon;select.onchange=()=>run(()=>setStyle('icon',select.value));fields.append(select);
  }
  toggle(fields,'圆角跟随主题',s.radius===null,v=>setStyle('radius',v?null:t.radius));if(s.radius!==null)field(fields,'控件圆角',s.radius,v=>setStyle('radius',v),'range',0,100);
  toggle(fields,'字号跟随主题',s.fontSize===null,v=>setStyle('fontSize',v?null:t.fontSize));if(s.fontSize!==null)field(fields,'控件字号',s.fontSize,v=>setStyle('fontSize',v),'number',10,72);
  field(fields,'自定背景',s.background||t.surface,v=>setStyle('background',v),'color');field(fields,'自定文字',s.color||t.text,v=>setStyle('color',v),'color');button(fields,'恢复主题颜色','undo-2',()=>edit(d=>{for(const x of d.nodes)if(chosen.has(x.id))x.style={...styleOf(x),background:null,color:null};}),'恢复主题颜色');
  if(n.kind==='super-list'){
    heading(fields,`列（${n.columns.length}）`);field(fields,'固定行高',n.rowHeight,v=>set('rowHeight',v),'number',20,200);
    n.columns.forEach((c,i)=>{const row=document.createElement('div');row.className='column-fields';field(row,`第 ${i+1} 列标题`,c.title,v=>edit(d=>d.nodes.find(x=>x.id===n.id).columns[i].title=v));field(row,'列宽',c.width,v=>edit(d=>d.nodes.find(x=>x.id===n.id).columns[i].width=v),'number',24,2000);button(row,`删除第 ${i+1} 列`,'trash-2',()=>edit(d=>d.nodes.find(x=>x.id===n.id).columns.splice(i,1)));fields.append(row);});
    button(fields,'添加列','plus',()=>edit(d=>d.nodes.find(x=>x.id===n.id).columns.push({title:'新列',width:120})),'添加列');
  }
}
function render(){
  chosen=new Set([...chosen].filter(id=>doc.nodes.some(n=>n.id===id)));if(!chosen.has(selected))selected=[...chosen].at(-1)||'';
  $('#title').value=doc.title;renderCanvas();renderPanel();renderProperties();$('#undo').disabled=!undo.length;$('#redo').disabled=!redo.length;$('#delete').disabled=!chosen.size;iconify();
  if(ready&&doc.sourcePage&&!sourceCanvas.ready&&!sourceLoading)run(()=>editSource(false));
}
function add(kind,x,y){
  if(!ready)return;const anchor=doc.sourcePage?selectedNode():null;const n=newComponent(kind,[...doc.nodes,...doc.baseline,...reservedIds],x??(anchor?anchor.x+16:24),y??(doc.sourcePage?(anchor?anchor.y+16:24):Math.max(8,...doc.nodes.map(n=>n.y+n.height))+16));n.style={align:n.kind==='button'?'center':'left',...n.style};n.width=Math.min(n.width,doc.width-48);n.x=Math.min(n.x,Math.max(0,doc.width-n.width));edit(d=>{d.nodes.push(n);d.height=Math.max(d.height,n.y+n.height+24);});selected=n.id;chosen=new Set([n.id]);render();$('#inspector').scrollTop=0;
}
function duplicate(){let ids;edit(d=>ids=duplicateNodes(d,[...chosen],reservedIds));chosen=new Set(ids);selected=ids.at(-1)||'';render();}
function group(){if(chosen.size<2)return;let i=1;while(doc.nodes.some(n=>styleOf(n).group==='group-'+i))i++;setStyle('group','group-'+i);}
function remove(){edit(d=>d.nodes=d.nodes.filter(n=>!chosen.has(n.id)||styleOf(n).locked));}
function undoAction(){if(undo.length){redo.push(clone(doc));doc=undo.pop();changed();}}
function redoAction(){if(redo.length){undo.push(clone(doc));doc=redo.pop();changed();}}
for(const [id,icon] of [['components','square'],['layers','layers'],['colors','palette'],['shapes','shapes'],['fonts','type'],['motion','play'],['ai','sparkles']]){const b=button($('#rail'),sections[id],icon,()=>{panel=id;renderPanel();$('#library').scrollTop=0;iconify();},sections[id]);b.dataset.panel=id;}
button($('#tools'),'保存设计稿','save',save);button($('#tools'),'导入设计稿','upload',()=>$('#file').click());button($('#tools'),'编辑现有网页','panels-top-left',editSource);
button($('#tools'),'新建设计稿','file-plus-2',()=>{if(confirm('清空当前设计稿？已有网页不会修改。')){remember();doc=newDesign();chosen.clear();selected='';changed();fit();}});
button($('#tools'),'生成 AI 开发任务','sparkles',exportTask,'AI 开发任务').className='primary-command';
const ct=$('#canvas-tools');choices(ct,'画布模式',[['select','选择','mouse-pointer-2'],['hand','抓手','hand']],mode,v=>{mode=v;for(const b of ct.querySelectorAll('[aria-pressed]'))b.setAttribute('aria-pressed',String(b.title===(mode==='select'?'选择':'抓手')));applyCamera();});
button(ct,'撤销','undo-2',undoAction).id='undo';button(ct,'重做','redo-2',redoAction).id='redo';button(ct,'复制控件','copy',duplicate);button(ct,'删除控件','trash-2',remove).id='delete';toggle(ct,'吸附',snap,v=>snap=v);
button($('#zoom-tools'),'缩小','minus',()=>zoomTo(camera.zoom/1.2));button($('#zoom-tools'),'实际尺寸',null,()=>zoomTo(1),'100%').id='zoom-value';button($('#zoom-tools'),'放大','plus',()=>zoomTo(camera.zoom*1.2));button($('#zoom-tools'),'适应窗口','maximize',fit);
$('#viewport').addEventListener('wheel',e=>{e.preventDefault();const r=$('#viewport').getBoundingClientRect();if(e.ctrlKey||e.metaKey)zoomTo(camera.zoom*Math.exp(-e.deltaY*.002),{x:e.clientX-r.left,y:e.clientY-r.top});else {camera.x-=e.deltaX;camera.y-=e.deltaY;applyCamera();}},{passive:false});
$('#viewport').onpointerdown=e=>{if(e.target.closest('#canvas-tools,#zoom-tools'))return;if(e.button!==1&&mode!=='hand'&&!space){if(!e.target.closest('.node')){chosen.clear();selected='';render();}return;}e.preventDefault();const el=$('#viewport'),sx=e.clientX,sy=e.clientY,x=camera.x,y=camera.y;el.setPointerCapture(e.pointerId);el.onpointermove=p=>{camera.x=x+p.clientX-sx;camera.y=y+p.clientY-sy;applyCamera();};el.onpointerup=el.onpointercancel=()=>el.onpointermove=null;};
$('#canvas').ondragover=e=>e.preventDefault();$('#canvas').ondrop=e=>{e.preventDefault();const kind=e.dataTransfer.getData('text/jade-kind'),r=$('#canvas').getBoundingClientRect();if(Object.hasOwn(TYPES,kind)||Object.hasOwn(PRESETS,kind))run(()=>add(kind,Math.max(0,Math.round((e.clientX-r.left)/camera.zoom)),Math.max(0,Math.round((e.clientY-r.top)/camera.zoom))));};
$('#title').onchange=e=>run(()=>edit(d=>d.title=e.target.value));
async function save(){
  if(!ready)throw Error('设计稿尚未读取成功');if(saving)throw Error('正在保存，请稍候');validate(doc);const snapshot=clone(doc),value=JSON.stringify(snapshot,null,2);saving=true;
  try{if(bridge){await native('design_save',value);status('已保存 · 待实现');}else {localStorage.setItem(projectKey+':saved',value);download('jade.design.json',value);status('已本地保存并导出 · 待实现');}lastSaved=JSON.stringify(snapshot);stash();if(lastSaved!==JSON.stringify(doc))status('部分修改尚未保存');return snapshot;}finally{saving=false;}
}
function importCurrent(){if(!sourceHtml)throw Error('当前工程没有可读取的 web/index.html');const next=importControls(sourceHtml,doc);remember();doc=next;changed();status('已读取控件身份，布局和列表列信息需要核对');}
async function editSource(record=true){
  if(!bridge)throw Error('请在易语言工程的 Jade设计中读取；独立设计稿没有工程路径');
  if(sourceLoading)return;
  sourceLoading=true;status('正在载入原网页画布…');
  try{
    const [html,base]=await native('design_edit_source');if(!html||!base)throw Error('支持库未返回原网页及资源路径，请更新支持库');
    sourceHtml=html;reservedIds=sourceIds(html);
    const found=await sourceCanvas.load(html,base,$('#canvas'));
    const next=clone(doc),oldSource=new Set(next.baseline.filter(n=>n.source).map(n=>n.id));
    for(const n of found){
      const existing=next.nodes.find(x=>x.id===n.id),baseline=next.baseline.find(x=>x.id===n.id);
      if(existing&&!existing.source){
        if(!baseline)throw Error('草稿控件 ID 与原网页冲突：'+n.id);
        const migrated=clone(n);for(const key of Object.keys(existing))if(key!=='note'&&JSON.stringify(existing[key])!==JSON.stringify(baseline[key]))migrated[key]=clone(existing[key]);
        migrated.source=true;next.nodes[next.nodes.indexOf(existing)]=migrated;next.baseline[next.baseline.indexOf(baseline)]=clone(n);
      }else if(!existing&&!oldSource.has(n.id)){next.nodes.push(n);next.baseline.push(clone(n));}
    }
    if(next.title==='新界面'&&sourceCanvas.title)next.title=sourceCanvas.title;
    next.sourcePage=true;validate(next);if(record)remember();doc=next;chosen.clear();selected='';changed();fit();status('原网页已载入画布 · 设计修改待保存');
  }catch(error){sourceCanvas.clear();status(error.message);throw error;}
  finally{sourceLoading=false;}
}
async function exportTask(){const snapshot=await save(),request=buildRequest(snapshot,spec,themeCss);$('#export textarea').value=request;$('#export').showModal();if(bridge){await native('design_export',request);status('任务与完整规范已写入工程');}}
function download(name,text){const url=URL.createObjectURL(new Blob([text],{type:'text/plain;charset=utf-8'}));const a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
$('#copy').onclick=async()=>{try{await navigator.clipboard.writeText($('#export textarea').value);status('完整 AI 任务已复制');}catch{status('复制失败，可下载任务文件');}};$('#download').onclick=()=>download('Jade-AI开发任务.md',$('#export textarea').value);
$('#file').onchange=async e=>{try{const f=e.target.files[0];if(!f)return;if(f.size>1024*1024)throw Error('设计稿超过 1 MB');const next=validate(JSON.parse(await f.text()));remember();doc=next;selected='';chosen.clear();changed();fit();}catch(error){status(error.message);}finally{e.target.value='';}};
document.addEventListener('keydown',e=>{
  if(!ready||$('#export').open)return;const typing=e.target.matches('input,textarea,select,[contenteditable]'),mod=e.ctrlKey||e.metaKey,key=e.key.toLowerCase();
  if(mod&&key==='s'){e.preventDefault();run(save);return;}if(typing)return;
  if(mod&&key==='z'){e.preventDefault();e.shiftKey?redoAction():undoAction();}else if(mod&&key==='d'){e.preventDefault();run(duplicate);}else if(key==='delete')run(remove);else if(key==='escape'){chosen.clear();selected='';render();}
  else if(key===' '){e.preventDefault();space=true;applyCamera();}else if(key==='0')fit();else if(key==='='||key==='+')zoomTo(camera.zoom*1.2);else if(key==='-')zoomTo(camera.zoom/1.2);
  else if(e.key.startsWith('Arrow')&&chosen.size){e.preventDefault();const step=e.shiftKey?8:1;run(()=>edit(d=>{for(const n of d.nodes)if(chosen.has(n.id)&&!styleOf(n).locked){n.x=Math.max(0,Math.min(d.width-n.width,n.x+(e.key==='ArrowLeft'?-step:e.key==='ArrowRight'?step:0)));n.y=Math.max(0,Math.min(d.height-n.height,n.y+(e.key==='ArrowUp'?-step:e.key==='ArrowDown'?step:0)));}}));}
});
document.addEventListener('keyup',e=>{if(e.key===' '){space=false;applyCamera();}});window.addEventListener('blur',()=>{space=false;applyCamera();});window.addEventListener('beforeunload',()=>{if(ready)stash();});
async function start(){try{let saved='',draft='';if(bridge){const values=await native('design_load');[saved,sourceHtml]=values;draft=values[3]||'';projectKey='jade-design:'+values[2];$('#project').textContent=values[2];}else{projectKey='jade-design:standalone';$('#project').textContent='独立设计稿';try{saved=localStorage.getItem(projectKey+':saved')||'';draft=localStorage.getItem(projectKey)||'';}catch{}}reservedIds=sourceIds(sourceHtml);if(saved)doc=validate(JSON.parse(saved));lastSaved=JSON.stringify(doc);if(draft&&draft!==JSON.stringify(doc)&&confirm('发现本地未保存草稿，是否恢复？'))doc=validate(JSON.parse(draft));const migrated=upgradeDefaultTheme(doc);ready=true;if(migrated)stash();status(migrated?'设计稿就绪 · 默认配色已改为黑白，待保存':JSON.stringify(doc)===lastSaved?'设计稿就绪 · 待实现':'本地草稿已恢复 · 待保存');render();fit();}catch(e){status('读取失败：'+e.message);}}
render();start();
