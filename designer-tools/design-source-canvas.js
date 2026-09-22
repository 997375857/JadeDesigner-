import {newNode,clone,styleOf} from './design-model.js';

const candidates='[data-jade-control],button,input,select,textarea,progress,[role="grid"],[role="tree"],a[id],label[id],h1[id],h2[id],h3[id],div[id],section[id],nav[id]';
function kindOf(e){
  const declared=e.getAttribute('data-jade-control');
  if(declared)return declared==='list'?'super-list':declared;
  if(e.matches('[role=grid],table'))return 'super-list';
  if(e.matches('[role=tree]'))return 'tree';
  if(e.matches('button,a'))return 'button';
  if(e.matches('select'))return 'select';
  if(e.matches('progress'))return 'progress';
  if(e.matches('input[type=checkbox]'))return 'checkbox';
  if(e.matches('input[type=radio]'))return 'radio';
  if(e.matches('input[type=range]'))return 'slider';
  if(e.matches('input[type=number]'))return 'number';
  if(e.matches('input,textarea'))return 'edit';
  return e.matches('div,section,nav')?'container':'label';
}

// Keep the original DOM/CSS isolated from editor styles. No project scripts or events execute.
export function sourceDocument(html,baseUrl){
  const d=new DOMParser().parseFromString(html,'text/html');
  for(const e of d.querySelectorAll('script,iframe,frame,object,embed,base,meta[http-equiv]'))e.remove();
  for(const e of d.querySelectorAll('*'))for(const a of [...e.attributes]){
    if(/^on/i.test(a.name)||['srcdoc','autofocus','formaction','action'].includes(a.name))e.removeAttribute(a.name);
  }
  const base=d.createElement('base');base.href=baseUrl;d.head.prepend(base);
  const csp=d.createElement('meta');csp.httpEquiv='Content-Security-Policy';
  const origin=new URL(baseUrl).origin;
  csp.content=`default-src 'none'; script-src 'none'; style-src 'unsafe-inline' ${origin}; img-src data: ${origin}; font-src data: ${origin}; media-src 'none'; frame-src 'none'; form-action 'none'`;
  d.head.prepend(csp);
  return '<!doctype html>'+d.documentElement.outerHTML;
}

export function createSourceCanvas(){
  const frame=document.createElement('iframe');frame.className='source-canvas-page';frame.title='原网页画布';
  frame.setAttribute('sandbox','allow-same-origin');frame.tabIndex=-1;
  let originals=new Map(),ready=false;
  const labelNode=e=>{
    if(e.matches('input,select,textarea,progress,div,section,nav'))return null;
    const walker=e.ownerDocument.createTreeWalker(e,4),texts=[];
    while(walker.nextNode()){const n=walker.currentNode;if(n.nodeValue.trim()&&!n.parentElement.closest('style,script,svg,i,[aria-hidden=true]'))texts.push(n);}
    return texts.length===1?texts[0]:null;
  };
  const nodes=()=>{
    const result=[],ids=new Set(),d=frame.contentDocument;
    for(const e of d.querySelectorAll('[id]')){if(ids.has(e.id))throw Error('原网页控件 ID 重复：'+e.id);ids.add(e.id);}
    for(const e of d.querySelectorAll(candidates)){
      const id=e.getAttribute('data-jade-id')||e.id;if(!id||id!==e.id)continue;
      const r=e.getBoundingClientRect(),cs=d.defaultView.getComputedStyle(e);
      if(r.width<1||r.height<1||r.x<0||r.y<0||r.x>4000||r.y>4000||cs.visibility==='hidden')continue;
      let n;try{n=newNode(kindOf(e),result);}catch{continue;}
      n.id=id;n.source=true;n.x=Math.round(r.x);n.y=Math.round(r.y);
      n.width=Math.max(24,Math.min(4000,Math.round(r.width)));n.height=Math.max(24,Math.min(4000,Math.round(r.height)));
      const text=labelNode(e);
      n.text=(e.matches('input,textarea')?(e.getAttribute('value')||''):e.matches('select')?(e.selectedOptions[0]?.textContent||''):(text?.nodeValue.trim()||e.getAttribute('aria-label')||e.id)).slice(0,120);
      n.placeholder=e.getAttribute('placeholder')||'';n.handler=e.getAttribute('data-jade-handler')||'';n.channel=e.getAttribute('data-jade-channel')||'';
      n.note='原网页控件；保持原 DOM、样式和业务逻辑。画布修改为设计意图，交给 AI 增量落实到原网页。';
      if(n.kind==='super-list'){
        const columns=[...e.querySelectorAll('th,[role=columnheader]')].map(h=>({title:h.textContent.trim(),width:Math.max(24,Math.min(2000,Math.round(h.getBoundingClientRect().width)))})).filter(c=>c.title);
        if(columns.length)n.columns=columns.slice(0,64);
      }
      originals.set(id,{element:e,node:clone(n),style:e.getAttribute('style'),text,textValue:text?.nodeValue});result.push(n);
      if(result.length===500)break;
    }
    return result;
  };
  return {
    frame,
    get ready(){return ready;},
    get title(){return frame.contentDocument?.title?.trim().slice(0,120)||'';},
    canEditText(id){const o=originals.get(id);return !!(o?.text||o?.element.matches('input,textarea'));},
    async load(html,baseUrl,host){
      ready=false;originals=new Map();
      const loaded=new Promise((resolve,reject)=>{
        const timer=setTimeout(()=>reject(Error('原网页样式载入超时')),15000);
        frame.onload=()=>{clearTimeout(timer);resolve();};
      });
      frame.srcdoc=sourceDocument(html,baseUrl);host.prepend(frame);await loaded;
      await Promise.race([frame.contentDocument.fonts.ready,new Promise(resolve=>setTimeout(resolve,2000))]);
      const result=nodes();ready=true;return result;
    },
    bounds(id){const e=originals.get(id)?.element;return e?.getBoundingClientRect();},
    hit(x,y){
      let e=frame.contentDocument?.elementFromPoint(x,y);
      while(e){if(originals.has(e.id))return e.id;e=e.parentElement;}return '';
    },
    scroll(x,y,dx,dy){
      const d=frame.contentDocument;let e=d?.elementFromPoint(x,y);
      while(e){const cs=d.defaultView.getComputedStyle(e);if(e.scrollHeight>e.clientHeight&&/auto|scroll/.test(cs.overflowY)){e.scrollBy(dx,dy);return;}e=e.parentElement;}
      d?.defaultView.scrollBy(dx,dy);
    },
    update(doc){
      if(!ready)return;
      const current=new Map(doc.nodes.filter(n=>n.source).map(n=>[n.id,n]));
      for(const [id,o]of originals){
        const e=o.element,n=current.get(id),b=o.node;
        if(o.style===null)e.removeAttribute('style');else e.setAttribute('style',o.style);
        if(o.text)o.text.nodeValue=o.textValue;
        if(!n){e.style.setProperty('display','none','important');continue;}
        const s=styleOf(n);
        if(s.hidden)e.style.setProperty('visibility','hidden','important');
        if(n.width!==b.width)e.style.setProperty('width',n.width+'px','important');
        if(n.height!==b.height)e.style.setProperty('height',n.height+'px','important');
        if(n.x!==b.x||n.y!==b.y)e.style.setProperty('translate',`${n.x-b.x}px ${n.y-b.y}px`,'important');
        if(n.text!==b.text&&o.text)o.text.nodeValue=n.text;
        if(e.matches('input,textarea')){e.value=n.text;e.setAttribute('placeholder',n.placeholder);}
        for(const [key,value]of [['background',s.background],['color',s.color],['font-size',s.fontSize===null?null:s.fontSize+'px'],['border-radius',s.radius===null?null:s.radius+'px']])if(value!==null)e.style.setProperty(key,value,'important');
      }
    },
    clear(){frame.remove();frame.srcdoc='';originals.clear();ready=false;}
  };
}
