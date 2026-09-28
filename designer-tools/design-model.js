import { parse } from 'parse5';
import {PRESETS,NAV_ICONS,isNavigation,navigationOf} from './design-components.js';

export const TYPES = {button:'按钮',edit:'编辑框',select:'下拉框','super-list':'超级列表框',tree:'树形框',checkbox:'复选框',radio:'单选框',tabs:'选项卡',progress:'进度条',slider:'滑块',number:'数值框',label:'文本',container:'区域'};
export const clone = value => JSON.parse(JSON.stringify(value));
export const DEFAULT_THEME={primary:'#27292d',background:'#ffffff',surface:'#ffffff',text:'#27292d',border:'#e5e5ea',radius:6,font:'Microsoft YaHei UI',fontSize:14,motion:150};
export const DEFAULT_STYLE={variant:'filled',radius:null,fontSize:null,bold:false,align:'left',icon:'',background:null,color:null,hidden:false,locked:false,group:''};
export const themeOf=d=>({...DEFAULT_THEME,...d.theme});
export function upgradeDefaultTheme(doc){
  const legacy=[
    {primary:'#157f68',background:'#f7f9fa',surface:'#ffffff',text:'#24313b',border:'#d6dfe4',radius:8,font:'Microsoft YaHei UI',fontSize:14,motion:150},
    {primary:'#16756f',background:'#f6f7f8',surface:'#ffffff',text:'#22272b',border:'#e2e7eb',radius:6,font:'Microsoft YaHei UI',fontSize:14,motion:150}
  ];
  if(doc.theme&&legacy.some(theme=>Object.keys(doc.theme).length===Object.keys(theme).length&&Object.entries(theme).every(([key,value])=>doc.theme[key]===value))){doc.theme=clone(DEFAULT_THEME);return true;}
  return false;
}
export const styleOf=n=>({...DEFAULT_STYLE,...n.style});
export function moveLayer(doc,id,delta){const index=doc.nodes.findIndex(n=>n.id===id);if(index<0)return;const to=Math.max(0,Math.min(doc.nodes.length-1,index+delta));const [node]=doc.nodes.splice(index,1);doc.nodes.splice(to,0,node);}
export function duplicateNodes(doc,ids,reserved=[]){
  const created=[];const groups=new Map();
  for(const n of [...doc.nodes])if(ids.includes(n.id)){
    const copy=clone(n);delete copy.source;delete copy.sourceLocator;copy.id=newNode(n.kind,[...doc.nodes,...doc.baseline,...reserved]).id;
    copy.x=Math.max(0,Math.min(doc.width-copy.width,copy.x+24));copy.y=Math.max(0,Math.min(doc.height-copy.height,copy.y+24));
    copy.style=styleOf(copy);copy.style.locked=false;
    if(copy.style.group){if(!groups.has(copy.style.group))groups.set(copy.style.group,`group-${copy.id}`);copy.style.group=groups.get(copy.style.group);}
    doc.nodes.push(copy);created.push(copy.id);
  }
  return created;
}
export function sourceIds(html) {
  const ids=[];
  const visit=n=>{for(const a of n.attrs||[])if(a.name==='id'||a.name==='data-jade-id')ids.push({id:a.value});for(const c of n.childNodes||[])visit(c);};
  visit(parse(html));return ids;
}
export function newDesign() { return {version:1,title:'新界面',width:1100,height:760,brief:'',theme:clone(DEFAULT_THEME),nodes:[],baseline:[]}; }
export function newNode(kind, nodes, x=24, y=24) {
  if (!Object.hasOwn(TYPES,kind)) throw Error('不支持的控件类型');
  let i=1; while(nodes.some(n=>n.id===`${kind}-${i}`))i++;
  return {id:`${kind}-${i}`,kind,text:TYPES[kind],x,y,width:kind==='super-list'?640:kind==='tree'?240:160,
    height:kind==='super-list'?280:kind==='tree'?200:40,placeholder:'',handler:'',channel:'',note:'',
    columns:kind==='super-list'?[{title:'序号',width:80},{title:'名称',width:180},{title:'内容',width:300}]:[],rowHeight:36};
}
export function newComponent(key,nodes,x=24,y=24){
  const preset=Object.hasOwn(PRESETS,key)?PRESETS[key]:null;
  const n=newNode(preset?.kind||key,nodes,x,y);
  if(preset){
    n.preset=key;n.text=preset.text??preset.label;
    for(const field of ['width','height','placeholder','style'])if(preset[field]!==undefined)n[field]=clone(preset[field]);
    if(isNavigation(n))n.navigation=navigationOf(n);
  }
  return n;
}
export function validate(doc) {
  const fail=message=>{throw Error(message);};
  if(!doc||doc.version!==1||!Array.isArray(doc.nodes)||!Array.isArray(doc.baseline))fail('不是版本 1 的 Jade 设计稿');
  if(doc.sourcePage!==undefined&&typeof doc.sourcePage!=='boolean')fail('原网页画布状态无效');
  const text=(v,max=4000)=>typeof v==='string'&&v.length<=max&&!v.includes('\0');
  const num=(v,min,max)=>Number.isFinite(v)&&v>=min&&v<=max;
  if(doc.theme!==undefined&&(!doc.theme||typeof doc.theme!=='object'||Array.isArray(doc.theme)))fail('主题设置无效');
  const color=v=>typeof v==='string'&&/^#[0-9a-f]{6}$/i.test(v);
  const theme=themeOf(doc);
  for(const key of ['primary','background','surface','text','border'])if(!color(theme[key]))fail('主题颜色无效');
  if(!num(theme.radius,0,100)||!num(theme.fontSize,12,36)||![0,150,250].includes(theme.motion)||!['Microsoft YaHei UI','Segoe UI','SimSun'].includes(theme.font))fail('主题设置无效');
  if(!text(doc.title,120)||!text(doc.brief,16000)||!num(doc.width,320,4000)||!num(doc.height,240,4000))fail('设计稿尺寸或说明无效');
  for(const list of [doc.nodes,doc.baseline]) {
    if(list.length>500)fail('最多 500 个控件');
    const ids=new Set();
    for(const n of list) {
      if(!n||!Object.hasOwn(TYPES,n.kind)||!text(n.id,128)||!n.id||/[\s"'<>`]/u.test(n.id)||ids.has(n.id))fail('控件 ID 无效或重复');
      ids.add(n.id);
      if(n.source!==undefined&&typeof n.source!=='boolean')fail('原网页控件来源无效');
      if(n.sourceLocator!==undefined&&(!n.source||!n.sourceLocator||typeof n.sourceLocator!=='object'||
        !text(n.sourceLocator.selector)||!n.sourceLocator.selector.startsWith('body > ')||!text(n.sourceLocator.text)))fail('原网页文字定位信息无效');
      if(n.preset!==undefined&&(!Object.hasOwn(PRESETS,n.preset)||PRESETS[n.preset].kind!==n.kind))fail('组件预设与控件类型不匹配');
      if(n.navigation!==undefined||isNavigation(n)){
        if(n.navigation!==undefined&&(!n.navigation||typeof n.navigation!=='object'||Array.isArray(n.navigation)))fail('导航配置无效');
        const nav=navigationOf(n);
        if(!isNavigation(n)||!nav||typeof nav.menu!=='boolean'||!Array.isArray(nav.items)||nav.items.length<1||nav.items.length>12)fail('导航配置无效');
        const itemIds=new Set();
        for(const item of nav.items){
          if(!item||!text(item.id,64)||!/^[a-zA-Z0-9_-]+$/.test(item.id)||itemIds.has(item.id)||!text(item.text,80)||!item.text.trim()||!Object.hasOwn(NAV_ICONS,item.icon))fail('导航项配置无效');
          itemIds.add(item.id);
        }
        if(!itemIds.has(nav.selectedId))fail('导航选中项无效');
      }
      for(const key of ['text','placeholder','handler','channel','note'])if(!text(n[key]))fail(`控件 ${n.id} 的 ${key} 无效`);
      if(!num(n.x,0,4000)||!num(n.y,0,4000)||!num(n.width,24,4000)||!num(n.height,24,4000))fail(`控件 ${n.id} 的位置或尺寸无效`);
      if(!Array.isArray(n.columns)||n.columns.length>64||!num(n.rowHeight,20,200))fail('列表列配置或行高无效');
      for(const c of n.columns)if(!c||!text(c.title,120)||!c.title.trim()||!num(c.width,24,2000))fail('每列都需要标题及有效宽度');
      if(n.kind==='super-list'&&!n.columns.length)fail('超级列表框至少需要一列');
      if(n.style!==undefined&&(!n.style||typeof n.style!=='object'||Array.isArray(n.style)))fail('控件样式无效');
      const s=styleOf(n);
      if(!['filled','tonal','outline','text'].includes(s.variant)||!['left','center','right'].includes(s.align)||
        (s.radius!==null&&!num(s.radius,0,100))||(s.fontSize!==null&&!num(s.fontSize,10,72))||
        (s.background!==null&&!color(s.background))||(s.color!==null&&!color(s.color))||
        !['','plus','search','check','download','settings','play','user','star'].includes(s.icon)||
        typeof s.bold!=='boolean'||typeof s.hidden!=='boolean'||typeof s.locked!=='boolean'||!text(s.group,128))fail('控件样式无效');
    }
  }
  return doc;
}
export function importControls(html, doc) {
  const next=clone(doc), added=[], seen=new Set();
  function walk(node) {
    const a=Object.fromEntries((node.attrs||[]).map(a=>[a.name,a.value]));
    if(a.id){if(seen.has(a.id))throw Error(`现有 HTML 含重复 ID：${a.id}`);seen.add(a.id);}
    let kind=a['data-jade-control']; if(kind==='list')kind='super-list';
    if(!kind&&node.tagName==='button')kind='button';
    const id=a['data-jade-id']||a.id;
    if(id&&Object.hasOwn(TYPES,kind)&&!next.nodes.some(n=>n.id===id)) {
      if(a['data-jade-id']&&a.id!==a['data-jade-id'])throw Error(`现有控件 ${id} 的 id 与 data-jade-id 不一致`);
      const n=newNode(kind,next.nodes,24,Math.max(8,...next.nodes.map(n=>n.y+n.height))+16);
      const allText=e=>e.nodeName==='#text'?e.value:(e.childNodes||[]).map(allText).join('');
      n.id=id;n.text=allText(node).trim().slice(0,120)||TYPES[kind];
      n.handler=a['data-jade-handler']||'';n.channel=a['data-jade-channel']||'';n.placeholder=a.placeholder||'';
      n.note='从现有 HTML 导入身份信息；画布位置不是原页面布局。修改时保留现有样式和业务逻辑。';
      if(kind==='super-list')n.note+=' 列信息需与现有 HTML/JS 核对后填写，当前列仅为草稿。';
      next.nodes.push(n);added.push(clone(n));next.height=Math.max(next.height,n.y+n.height+24);
    }
    for(const c of node.childNodes||[])walk(c);
  }
  walk(parse(html));next.baseline.push(...added);return validate(next);
}
const TASK_FIELDS=['text','x','y','width','height','placeholder','handler','channel','note','preset','columns','rowHeight','navigation'];
const TASK_STYLE_FIELDS=['variant','radius','fontSize','bold','align','icon','background','color'];
const DATA_KINDS=new Set(['edit','select','super-list','tree','checkbox','radio','tabs','progress','slider','number']);
const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const taskStyle=(node,baseline)=>{
  const style=styleOf(node),before=baseline?styleOf(baseline):DEFAULT_STYLE;
  return Object.fromEntries(TASK_STYLE_FIELDS.filter(key=>!same(style[key],before[key])).map(key=>[key,style[key]]));
};
const taskLocator=node=>node.sourceLocator?{selector:node.sourceLocator.selector,originalText:node.sourceLocator.text}:{id:node.id};
function addedTask(node){
  const result={id:node.id,kind:node.kind,text:node.text,x:node.x,y:node.y,width:node.width,height:node.height};
  if(node.preset)result.preset=node.preset;
  for(const key of ['placeholder','handler','channel','note'])if(node[key]&&!node[key].startsWith('原网页控件；'))result[key]=node[key];
  if(node.kind==='super-list'){result.columns=node.columns;result.rowHeight=node.rowHeight;}
  if(node.navigation)result.navigation=node.navigation;
  const style=taskStyle(node);if(Object.keys(style).length)result.style=style;
  return result;
}
function changedTask(node,before){
  const fields={};
  for(const key of TASK_FIELDS){
    if(key==='note'&&before.note===node.note)continue;
    if(!same(node[key],before[key]))fields[key]=key==='text'?{from:before[key],to:node[key]}:node[key]??null;
  }
  const style=taskStyle(node,before);if(Object.keys(style).length)fields.style=style;
  return Object.keys(fields).length?{kind:node.kind,locate:taskLocator(before),fields}:null;
}
export function taskChanges(doc,sourceHtml=''){
  validate(doc);
  const old=new Map(doc.baseline.map(n=>[n.id,n])),current=new Set(doc.nodes.map(n=>n.id));
  const localIds=new Set(sourceHtml?sourceIds(sourceHtml).map(n=>n.id):[]);
  const added=[],review=[];
  for(const node of doc.nodes.filter(n=>!old.has(n.id))){
    const existingId=localIds.has(node.id)?node.id:'';
    if(existingId)review.push({draftId:node.id,kind:node.kind,existingId,intent:addedTask(node)});
    else added.push(addedTask(node));
  }
  const changed=doc.nodes.filter(n=>old.has(n.id)).map(n=>changedTask(n,old.get(n.id))).filter(Boolean);
  const removed=doc.baseline.filter(n=>!current.has(n.id)).map(n=>({kind:n.kind,locate:taskLocator(n)}));
  const theme=Object.fromEntries(Object.entries(themeOf(doc)).filter(([key,value])=>!same(value,DEFAULT_THEME[key])));
  return {added,changed,removed,review,theme};
}
export function buildRequest(doc,sourceHtml=''){
  const changes=taskChanges(doc,sourceHtml);
  const {added,changed,removed,review,theme}=changes;
  const page={};
  if(!doc.sourcePage&&!doc.baseline.length&&doc.title!=='新界面')page.title=doc.title;
  if(doc.width!==1100)page.width=doc.width;
  if(doc.height!==760)page.height=doc.height;
  if(!added.length&&!changed.length&&!removed.length&&!review.length&&!Object.keys(theme).length&&!Object.keys(page).length&&!doc.brief.trim())
    return '# Jade 开发任务\n\n当前设计稿与导入基线一致，没有待实现的变更。\n';
  const lines=['# Jade 开发任务','',
    '请对照当前工程 web/index.html、style.css、app.js 和已有易语言绑定，只实现以下设计变更；保留其他页面、控件 ID、通讯通道及业务逻辑。设计稿只表达需求，完成后到 Jade预览和易语言实际验证。'];
  if(doc.brief.trim())lines.push('',`需求：${doc.brief.trim()}`);
  if(Object.keys(page).length)lines.push('',`页面设置：${JSON.stringify(page)}`);
  if(!doc.sourcePage&&!doc.baseline.length&&added.length)lines.push('',
    '新页面沿用设计稿的黑白默认外观；通用组件样式可参考 JadeHybrid 源码中的 designer-tools/jade-ui-theme.css。');
  if(added.length)lines.push('',`新增：${JSON.stringify(added)}`);
  if(changed.length)lines.push('',`修改：${JSON.stringify(changed)}`);
  if(removed.length)lines.push('',`删除：${JSON.stringify(removed)}`);
  if(review.length)lines.push('',`待核对（网页已有同名控件，勿直接重复创建）：${JSON.stringify(review)}`);
  if(Object.keys(theme).length)lines.push('',`主题调整：${JSON.stringify(theme)}`);
  const existingIds=new Set(sourceHtml?sourceIds(sourceHtml).map(n=>n.id):[]);
  const sameText=added.filter(n=>n.text&&n.text!==n.id&&existingIds.has(n.text)).map(n=>({newId:n.id,text:n.text,existingId:n.text}));
  if(sameText.length)lines.push('',`控件身份核对：${JSON.stringify(sameText)}。新控件的显示文字与原网页 ID 同名，但不是同一个控件；保留原 ID 对应的 DOM 和业务逻辑。`);
  const affected=[...added,...changed];
  const data=affected.filter(n=>DATA_KINDS.has(n.kind));
  const lists=data.filter(n=>n.kind==='super-list');
  const hasLocator=[...changed,...removed].some(n=>n.locate.selector);
  if(data.length||hasLocator||affected.some(n=>n.handler||n.channel||n.fields?.handler||n.fields?.channel))lines.push('','对接要求：');
  if(hasLocator)lines.push('- 无 ID 原文字按 selector 查找并核对 originalText；不匹配则停止，不改相邻图标和结构。locate.id 才是原 HTML 控件 ID。');
  if(data.length)lines.push('- 涉及数据控件须使用唯一 id、相同的 data-jade-id 和对应 data-jade-control；在 app.js 中处理 jade:data-control:update 的 controlId、action、payload，完成真实页面更新。保留既有适配器。');
  if(added.some(n=>n.kind==='edit'&&n.preset==='textarea'))lines.push('- preset 为 textarea 的新增控件是可输入的多行编辑框，使用 textarea；text 是初始内容，placeholder 是空值提示，不要生成静态 div。');
  if(lists.length)lines.push('- 超级列表框按列标题实现虚拟滚动、固定行高、可视行与缓冲、beginBatch/endBatch；插入、删除和修改只更新受影响的行。易语言 Jade超级列表框绑定 使用同一控件 ID。');
  if(affected.some(n=>n.handler||n.channel||n.fields?.handler||n.fields?.channel))lines.push('- 业务事件的 data-jade-handler、data-jade-channel 与实际 jade.invoke 通道保持一致。');
  lines.push('','完整接口细节按 JadeHybrid 源码中的 docs/AI生成UI与JadeView支持库对接规范.md 核对；仅报告本次改动和未验证项。');
  return lines.join('\n')+'\n';
}
