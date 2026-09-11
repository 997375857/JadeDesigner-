import {parse} from 'parse5';

export const controls='button,label,span,p,h1,h2,h3,h4,h5,h6,input,select,textarea,progress,img,div,section,main,article,aside,header,footer,nav,form';
export const containers='div,section,main,article,aside,header,footer,nav,form,body';
const blocked=new Set(['script','style','template','textarea','svg','math','iframe','noscript','xmp']);
const attrs=node=>(node.attrs||[]).map(a=>[a.name,a.value]).sort(([a],[b])=>a.localeCompare(b));
const texts=node=>(node.childNodes||[]).filter(n=>n.nodeName==='#text'&&n.value.trim()).map(n=>n.value);
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
const sourceIds=node=>{
  const result=[];
  const visit=n=>{if(blocked.has(n.tagName))return;if(n!==node){const id=n.attrs?.find(a=>a.name==='id')?.value;if(id)result.push(id);}for(const child of n.childNodes||[])visit(child);};
  visit(node);return result;
};
function pathOf(element) {
  const path=[];
  for(let n=element;n;n=n.parentElement){const siblings=[...(n.parentNode?.children||[])].filter(s=>s.localName===n.localName);path.unshift({tag:n.localName,index:siblings.indexOf(n)});}
  return path;
}
export function describeElement(element) {
  if(!(element instanceof Element)||element.getRootNode()!==document||element.closest('script,style,template,svg,math,iframe,[contenteditable]'))throw new Error('该区域不能对应到静态 HTML 控件');
  const ancestors=[];for(let n=element.parentElement;n;n=n.parentElement)if(n.id)ancestors.push({path:pathOf(n),id:n.id});
  const ids=[...element.querySelectorAll('[id]')].filter(n=>n.id&&!n.closest('script,style,template,svg,math,iframe,noscript,xmp')).map(n=>n.id);
  return {path:pathOf(element),id:element.id,attrs:[...element.attributes].map(a=>[a.name,a.value]).sort(([a],[b])=>a.localeCompare(b)),texts:[...element.childNodes].filter(n=>n.nodeType===3&&n.nodeValue.trim()).map(n=>n.nodeValue),ancestors,ids};
}
export function atPath(root,path) {
  let node=root;
  for(let i=0;i<path.length;i++) {
    const step=path[i];if(blocked.has(step.tag)&&i<path.length-1)throw new Error('不修改脚本、模板或框架内部元素');
    node=(node.childNodes||[]).filter(n=>n.tagName===step.tag)[step.index];
    if(!node)throw new Error('控件是动态生成的，或网页结构已经改变');
  }
  return node;
}
export function resolveElement(source,descriptor) {
  const errors=[];const root=parse(source,{sourceCodeLocationInfo:true,onParseError:e=>errors.push(e)});
  const node=atPath(root,descriptor.path),location=node.sourceCodeLocation;
  if(!location?.startTag)throw new Error('没有可靠的控件源码位置，HTML 可能省略了开始标签');
  if(!equal(attrs(node),descriptor.attrs)||!equal(texts(node),descriptor.texts))throw new Error('控件属性或文字与 HTML 不一致，未猜测修改');
  if(errors.some(e=>e.code==='duplicate-attribute'&&e.startOffset>=location.startTag.startOffset&&e.startOffset<location.startTag.endOffset))throw new Error('控件含有重复属性，请先修正 HTML');
  const byId=new Map();
  const index=n=>{for(const a of n.attrs||[])if(a.name==='id'&&a.value){const matches=byId.get(a.value)||[];matches.push(n);byId.set(a.value,matches);}for(const c of n.childNodes||[])index(c);};index(root);
  let scope=root;
  if(!descriptor.id)for(const ancestor of descriptor.ancestors||[]) {
    const matches=byId.get(ancestor.id)||[];
    if(matches.length!==1)throw new Error('所属区域的 id 重复或已改变，未修改');
    const anchor=atPath(root,ancestor.path);
    if(anchor!==matches[0])throw new Error('所属区域的位置已改变，未修改');
    scope=anchor;break;
  }
  // Identical wrappers in different sections are not the same insertion target.
  // Keep refusing genuinely identical siblings, and validate descendant IDs so
  // a dynamically populated/replaced container cannot masquerade as the source.
  if(!descriptor.id&&descriptor.ids&&!equal(sourceIds(node),descriptor.ids))throw new Error('区域内部控件与 HTML 不一致，未猜测添加位置');
  let count=0;
  if(descriptor.id)count=(byId.get(descriptor.id)||[]).length;
  else {
    const visit=n=>{if(n.tagName===node.tagName&&equal(attrs(n),descriptor.attrs)&&equal(texts(n),descriptor.texts)&&(!descriptor.ids||equal(sourceIds(n),descriptor.ids)))++count;for(const c of n.childNodes||[])visit(c);};visit(scope);
  }
  if(count!==1)throw new Error('存在无法区分的同名或同样控件，请先为控件设置唯一 id');
  return {root,node,location};
}
export function sourceEdit(source,node,kind,key,value) {
  const location=node.sourceCodeLocation?.startTag;if(!location)throw new Error('没有可靠的控件源码位置');
  return {kind,start:location.startOffset,end:location.endOffset,old:source.slice(location.startOffset,location.endOffset),key,value:String(value)};
}
export function propertyEdits(source,descriptor,changes) {
  const {node}=resolveElement(source,descriptor);const edits=[];
  for(const change of changes) {
    if(change.kind==='text') {
      const textParent=change.descriptor?resolveElement(source,change.descriptor).node:node;
      const content=(textParent.childNodes||[]).filter(n=>n.nodeName==='#text'&&n.value.trim());
      if(content.length!==1||blocked.has(textParent.tagName))throw new Error('不是单一静态标题，不能自动修改');
      const t=content[0],l=t.sourceCodeLocation;
      const value=t.value.match(/^\s*/)[0]+change.value+t.value.match(/\s*$/)[0];
      edits.push({kind:'text',start:l.startOffset,end:l.endOffset,old:source.slice(l.startOffset,l.endOffset),key:'',value});
    } else if(change.kind==='select') {
      const options=(node.childNodes||[]).filter(n=>n.tagName==='option');
      if(node.tagName!=='select'||options.length!==change.count)throw new Error('动态选项或分组选项暂不自动修改');
      options.forEach((option,index)=>{const selected=index===Number(change.value);if(option.attrs.some(a=>a.name==='selected')!==selected)edits.push(sourceEdit(source,option,'attribute','selected',selected?'1':'0'));});
    } else edits.push(sourceEdit(source,node,change.kind,change.key,change.value));
  }
  return edits;
}
export function insertionEdit(source,descriptor,kind,liveIds=[],minimum=1) {
  const {root,node,location}=resolveElement(source,descriptor);
  if(!containers.split(',').includes(node.tagName)||!location.endTag)throw new Error('选择有明确结束标签的布局容器，或追加到页面末尾');
  const occupied=new Set(liveIds),handlers=new Set();
  const visit=n=>{for(const a of n.attrs||[]){if(a.name==='id')occupied.add(a.value);if(a.name==='data-jade-handler')handlers.add(a.value);}for(const c of n.childNodes||[])visit(c);};visit(root);
  let number=minimum;
  const names={button:'按钮',label:'标签',heading:'标题',input:'输入框',textarea:'多行输入框',checkbox:'复选框',radio:'单选框',select:'选择框',container:'容器',progress:'进度条'};
  if(!names[kind])throw new Error('未知控件类型');
  const suffix=kind==='button'?'被单击':kind==='select'?'选择项被改变':'选中状态被改变';
  while(occupied.has(`jade_${kind}_${number}`)||handlers.has(`界面${names[kind]}${number}_${suffix}`))++number;
  if(number>999999)throw new Error('没有可用的控件编号');
  const l=location.endTag;
  return {kind:'insert',start:l.startOffset,end:l.endOffset,old:source.slice(l.startOffset,l.endOffset),key:kind,value:String(number)};
}
export const encodeEdits=edits=>edits.map(e=>[e.kind,e.start,e.end,e.old,e.key,e.value].map(x=>encodeURIComponent(String(x))).join('\t')).join('\n');
export function applySavedProperties(element,source,descriptor,changes) {
  const node=atPath(parse(source),descriptor.path),updated=new Map(attrs(node));
  for(const change of changes)if(change.kind==='text'&&change.descriptor) {
    const target=change.target||element;
    if(!target.isConnected||!equal(describeElement(target),change.descriptor))throw new Error('HTML 已保存，但页面标题已被脚本修改，请重新加载核对');
  }
  for(const change of changes) {
    if(change.kind==='style')element.setAttribute('style',updated.get('style'));
    else if(change.kind==='text'){
      const target=change.target||element;
      const textParent=change.descriptor?atPath(parse(source),change.descriptor.path):node;
      const text=[...target.childNodes].find(n=>n.nodeType===3&&n.nodeValue.trim());if(text)text.nodeValue=texts(textParent)[0];
    } else if(change.kind==='attribute') {
      if(updated.has(change.key))element.setAttribute(change.key,updated.get(change.key));else element.removeAttribute(change.key);
      if(change.key==='checked')element.checked=change.value==='1';
      if(change.key==='value'&&'value'in element)element.value=change.value;
    } else if(change.kind==='select') {
      [...element.options].forEach((option,index)=>option.toggleAttribute('selected',index===Number(change.value)));element.selectedIndex=Number(change.value);
    }
  }
}
