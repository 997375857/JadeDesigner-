import {createElement,MousePointer2,Type,Heading2,TextCursorInput,ListFilter,CheckSquare,SquareDot,AlignLeft,Square,ChartNoAxesColumn,Save,Undo2,ArrowUp,ArrowDown,ArrowLeft,ArrowRight,RotateCcw,Copy,ScanSearch} from 'lucide';
import {controls,containers,describeElement,resolveElement,propertyEdits,insertionEdit,encodeEdits,applySavedProperties} from './visual-model.js';
import {elementDescription} from './element-description.js';

export function createVisualDesigner({root,panel,body,foot,status,open,request,isTool,isDesign,notify}) {
  let selected=null,selectionVersion=0,documentRevision='',source='',descriptor=null,saving=false,visibleView='properties';
  let armedKind='';
  let panelRequested=false;
  let inspecting=false,picking=false,sourcePath='',sourceError='',changeRequest='';
  const placementStyle=document.createElement('style');placementStyle.textContent='html body,html body *{cursor:crosshair!important}';
  const disarm=()=>{armedKind='';picking=false;window.__jadeElementPicking=false;placementStyle.remove();};
  const el=(tag,text)=>{const n=document.createElement(tag);if(text)n.textContent=text;return n;};
  const button=(text,fn,icon)=>{const n=el('button',text);n.type='button';n.addEventListener('click',fn);if(icon)n.prepend(createElement(icon));return n;};
  const style=el('style');style.textContent=`
    .tools-panel.visual-panel{width:min(360px,calc(100vw - 24px))}
    .visual-panel .tools-body{padding:12px}.visual-panel .tools-foot{padding:8px 12px}.visual-panel .tools-foot button{display:inline-flex;align-items:center;gap:7px}
    .visual-picker{display:flex;align-items:center;gap:6px}.visual-picker select{flex:1}.visual-picker button{width:34px;height:34px;flex:0 0 34px;padding:7px;display:flex;align-items:center;justify-content:center}
    .visual-panel svg{width:17px;height:17px;flex:0 0 17px}.visual-panel .visual-fields{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:12px 10px}
    .visual-panel .visual-field{display:flex;flex-direction:column;align-items:stretch;gap:5px;margin:0;font-size:13px;min-width:0}.visual-field.wide{grid-column:1/-1}.visual-field span{overflow-wrap:anywhere}
    .visual-panel input,.visual-panel select{min-width:0;width:100%;font:inherit;border:1px solid #b9c8c1;border-radius:4px;background:white;color:inherit;padding:6px;height:34px}
    .visual-panel input[type=checkbox]{width:18px;height:18px;padding:0}.visual-panel input[type=color]{padding:3px;cursor:pointer}.visual-panel .visual-field.boolean{flex-direction:row;align-items:center;gap:8px}
    .visual-panel .visual-tabs{display:flex;gap:6px;margin:12px 0}.visual-tabs button{flex:1;padding:6px}.visual-tabs [aria-selected=true]{background:#e2f2e8;border-color:#16784b}
    .visual-palette{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:8px}.visual-palette button{display:flex;align-items:center;gap:8px;padding:10px 8px;min-height:42px;text-align:left;box-shadow:none;overflow-wrap:anywhere}
    .visual-identity{font-size:12px;overflow-wrap:anywhere;margin:10px 0;color:#68736c}.visual-note{font-size:12px;color:#68736c;margin:8px 0}.visual-selection{position:fixed;border:2px solid #138757;pointer-events:none;box-sizing:border-box;border-radius:2px;z-index:-1}
    .visual-panel .visual-save-note{font-size:12px;overflow-wrap:anywhere}
    .visual-nudge{grid-column:1/-1;display:flex;align-items:center;gap:6px;flex-wrap:wrap}.visual-nudge button{width:32px;height:32px;padding:6px;display:flex;align-items:center;justify-content:center}.visual-nudge .visual-field{flex:1;min-width:70px}.visual-nudge .visual-field input{width:70px}
    .visual-panel .element-description{height:230px;min-height:100px;margin:10px 0}.visual-panel .element-request{height:90px;min-height:60px;margin:0}.visual-panel .element-request-label{display:flex;flex-direction:column;align-items:stretch;gap:6px;font-size:13px;margin:8px 0}.visual-description-actions{display:flex;gap:8px;flex-wrap:wrap}
  `;root.append(style);
  const outline=el('div');outline.className='visual-selection';outline.hidden=true;root.append(outline);
  const position=()=>{
    if(!selected?.isConnected||(!isDesign()&&!inspecting)){outline.hidden=true;return;}
    const r=selected.getBoundingClientRect();outline.hidden=!r.width||!r.height;
    Object.assign(outline.style,{left:r.left+'px',top:r.top+'px',width:r.width+'px',height:r.height+'px'});
  };
  window.addEventListener('scroll',position,true);window.addEventListener('resize',position);
  const observer=new ResizeObserver(position);
  const typeOf=e=>({button:'按钮',label:'标签',span:'文字',p:'段落',input:'输入框',select:'下拉框',textarea:'多行输入',progress:'进度条',img:'图片',div:'布局区域',section:'区域',main:'主区域',body:'页面',form:'表单'}[e.localName]||(/^h[1-6]$/.test(e.localName)?'标题':'区域'));
  const labelOf=e=>((e.matches('button,label,span,p,h1,h2,h3,h4,h5,h6')?(e.textContent||'').trim().slice(0,24):'')||e.getAttribute('data-jade-handler')||e.id||typeOf(e));
  const fail=error=>{if(!panelRequested&&!inspecting){notify(error.message,true);return;}status.textContent=error.message;status.className='error';};
  const stillSelected=version=>version===selectionVersion&&(isDesign()||inspecting)&&((!panelRequested&&!inspecting)||panel.classList.contains('visual-panel'));
  const captionOf=element=>{
    const root=element.matches('input[type=checkbox],input[type=radio]')?element.closest('label'):element;
    if(!root||root.matches('select,textarea,input,img,progress'))return null;
    const walker=document.createTreeWalker(root,NodeFilter.SHOW_TEXT),nodes=[];
    for(let n=walker.nextNode();n;n=walker.nextNode())if(n.nodeValue.trim()&&!n.parentElement.closest('svg,i,script,style,template,select,textarea'))nodes.push(n);
    if(nodes.length!==1)return null;
    return {target:nodes[0].parentElement,descriptor:describeElement(nodes[0].parentElement),text:nodes[0].nodeValue};
  };
  const current=()=>{
    if(!selected?.isConnected||JSON.stringify(describeElement(selected))!==JSON.stringify(descriptor))throw new Error('页面控件已发生变化，请重新选中后修改');
  };
  function shell() {
    open(panelRequested?'可视化设计':'元素描述');panel.classList.add('visual-panel');
    const picker=el('select');picker.setAttribute('aria-label','选择控件');picker.append(new Option('选择控件',''));
    const choices=[...document.querySelectorAll(controls)].filter(e=>!e.closest('jade-common-tools,jade-runtime-diagnostics,script,style,template,svg,[contenteditable]')).slice(0,500);
    choices.forEach((e,i)=>{const option=new Option(labelOf(e)+' · '+typeOf(e),String(i));picker.append(option);if(e===selected)picker.value=String(i);});
    picker.addEventListener('change',()=>{if(picker.value!=='')select(choices[Number(picker.value)]);});
    const up=button('',()=>select(selected.parentElement),ArrowUp);up.title='选择上级区域';up.setAttribute('aria-label',up.title);up.disabled=!selected?.parentElement?.matches(controls);
    const pickerRow=el('div');pickerRow.className='visual-picker';pickerRow.append(picker,up);body.append(pickerRow);
    const tabs=el('div');tabs.className='visual-tabs';tabs.setAttribute('role','tablist');
    const views=panelRequested?[['属性','properties'],['控件','palette'],['AI 描述','description']]:[['AI 描述','description']];
    for(const [name,view]of views){const b=button(name,()=>{visibleView=view;render();});b.setAttribute('role','tab');b.setAttribute('aria-selected',String(visibleView===view));tabs.append(b);}body.append(tabs);
  }
  function field(parent,name,type,value,{wide=false,options=null}={}) {
    const label=el('label');label.className='visual-field'+(wide?' wide':'')+(type==='checkbox'?' boolean':'');
    const input=el(options?'select':'input');input.setAttribute('aria-label',name);
    if(options)for(const [v,t]of options)input.append(new Option(t,v));else input.type=type;
    if(type==='checkbox')input.checked=Boolean(value);else input.value=value;
    label.append(el('span',name),input);parent.append(label);return input;
  }
  function renderProperties() {
    if(!selected||!descriptor){body.append(el('p','未选择控件'));return;}
    const metadata=el('div',labelOf(selected));metadata.className='visual-identity';body.append(metadata);
    if(selected.dataset.jadeHandler){const bound=el('div','回调：'+selected.dataset.jadeHandler);bound.className='visual-identity';body.append(bound);}
    const fields=el('div');fields.className='visual-fields';body.append(fields);
    const changes=[];const watch=(input,kind,key,transform=v=>v)=>{
      const value=()=>input.type==='checkbox'?(input.checked?'1':'0'):input.value;
      const initial=value();changes.push(()=>value()===initial?null:{kind,key,value:transform(value())});
    };
    const caption=captionOf(selected);
    if(caption) {
      const title=field(fields,'标题文字','text',caption.text.trim(),{wide:true}),initial=title.value;
      changes.push(()=>title.value===initial?null:{kind:'text',key:'',value:title.value,target:caption.target,descriptor:caption.descriptor});
    }
    if(selected.matches('input[type=button],input[type=submit]'))watch(field(fields,'按钮标题','text',selected.getAttribute('value')||'',{wide:true}),'attribute','value');
    const cs=getComputedStyle(selected);
    const translation=cs.translate==='none'?['0px','0px']:cs.translate.trim().split(/\s+/);
    if(translation.length===1)translation.push('0px');
    if((cs.display!=='inline'||selected.matches('img,input,textarea,select,button'))&&translation.length===2&&translation.every(v=>/^-?\d+px$/.test(v)&&Math.abs(parseInt(v))<=2000)) {
      const x=field(fields,'横向偏移','number',String(parseInt(translation[0]))),y=field(fields,'纵向偏移','number',String(parseInt(translation[1])));
      for(const input of [x,y]){input.min='-2000';input.max='2000';input.step='1';}
      const initial=[x.value,y.value];
      changes.push(()=>x.value===initial[0]&&y.value===initial[1]?null:{kind:'style',key:'translate',value:x.value+'px '+y.value+'px'});
      const nudge=el('div');nudge.className='visual-nudge';fields.append(nudge);
      const step=field(nudge,'步长','number','1');step.min='1';step.max='100';step.step='1';
      for(const [title,shape,dx,dy]of [['左移',ArrowLeft,-1,0],['上移',ArrowUp,0,-1],['下移',ArrowDown,0,1],['右移',ArrowRight,1,0],['重置位移',RotateCcw,0,0]]) {
        const b=button('',()=>{
          if(!step.checkValidity()||!x.checkValidity()||!y.checkValidity())return;
          x.value=String(dx||dy?Math.max(-2000,Math.min(2000,Number(x.value)+dx*Number(step.value))):0);
          y.value=String(dx||dy?Math.max(-2000,Math.min(2000,Number(y.value)+dy*Number(step.value))):0);
          status.textContent='位置修改待保存';
        },shape);b.title=title;b.setAttribute('aria-label',title);nudge.append(b);
      }
    }
    const sizeOptions=[['auto','自动'],['100%','铺满'],['fixed','指定像素']];
    for(const [name,key]of [['宽度','width'],['高度','height']]) {
      const inline=selected.style.getPropertyValue(key),initial=/^\d+px$/.test(inline)?'fixed':inline==='100%'?'100%':'auto';
      const mode=field(fields,name,'select',initial,{options:sizeOptions});
      const number=field(fields,name+'像素','number',String(Math.round(parseFloat(cs[key])||0)));number.min='0';number.max='4000';number.step='1';number.disabled=mode.value!=='fixed';
      mode.addEventListener('change',()=>number.disabled=mode.value!=='fixed');const before=number.value;
      changes.push(()=>initial===mode.value&&before===number.value?null:{kind:'style',key,value:mode.value==='fixed'?number.value+'px':mode.value});
    }
    const font=field(fields,'字号','number',String(Math.round(parseFloat(cs.fontSize))));font.min='8';font.max='160';font.step='1';watch(font,'style','font-size',v=>v+'px');
    const toHex=value=>{const parts=value.match(/[\d.]+/g);return parts&&parts.length>=3?'#'+parts.slice(0,3).map(n=>Math.round(Number(n)).toString(16).padStart(2,'0')).join(''):'#ffffff';};
    watch(field(fields,'文字颜色','color',toHex(cs.color)),'style','color');watch(field(fields,'背景颜色','color',toHex(cs.backgroundColor)),'style','background-color');
    watch(field(fields,'隐藏','checkbox',cs.visibility==='hidden'),'style','visibility',v=>v==='1'?'hidden':'visible');
    if(selected.matches('button,input,select,textarea'))watch(field(fields,'禁用','checkbox',selected.hasAttribute('disabled')),'attribute','disabled');
    if(selected.matches('input,textarea')) {
      watch(field(fields,'提示文字','text',selected.getAttribute('placeholder')||'',{wide:true}),'attribute','placeholder');
      watch(field(fields,'只读','checkbox',selected.hasAttribute('readonly')),'attribute','readonly');
      if(selected.matches('input:not([type=password]):not([type=file]):not([type=checkbox]):not([type=radio]):not([type=button]):not([type=submit])'))watch(field(fields,'默认内容','text',selected.getAttribute('value')||'',{wide:true}),'attribute','value');
      if(selected.matches('input[type=checkbox],input[type=radio]'))watch(field(fields,'默认选中','checkbox',selected.hasAttribute('checked')),'attribute','checked');
    }
    if(selected.matches('select:not([multiple])')&&selected.children.length===selected.options.length&&selected.options.length<=20) {
      const defaultIndex=[...selected.options].findIndex(o=>o.hasAttribute('selected'));
      const choice=field(fields,'默认选项','select',String(Math.max(0,defaultIndex)),{wide:true,options:[...selected.options].map((o,i)=>[String(i),o.textContent])});const initial=choice.value;
      changes.push(()=>choice.value===initial?null:{kind:'select',value:choice.value,count:selected.options.length});
    }
    if(selected.matches(containers)&&selected.localName!=='body') {
      const direction=field(fields,'排列','select',cs.display==='flex'?cs.flexDirection:'unchanged',{wide:true,options:[['unchanged','保持原布局'],['row','左右排列'],['column','上下排列']]});const initial=direction.value;
      changes.push(()=>direction.value===initial||direction.value==='unchanged'?null:[{kind:'style',key:'display',value:'flex'},{kind:'style',key:'flex-direction',value:direction.value}]);
      for(const [name,key]of [['间距','gap'],['内边距','padding']]){const n=field(fields,name,'number',String(parseInt(cs[key])||0));n.min='0';n.max='128';n.step='1';watch(n,'style',key,v=>v+'px');}
    }
    const save=button('保存属性',async()=>{
      if(saving)return;
      const version=selectionVersion,target=selected,snapshot=descriptor;saving=true;save.disabled=true;
      try {
        current();for(const input of fields.querySelectorAll('input'))if(!input.checkValidity())throw new Error('属性值超出允许范围');
        const edits=changes.flatMap(get=>get()||[]);if(!edits.length){status.textContent='没有修改';return;}
        for(const change of edits.filter(e=>e.kind==='style')) {
          const candidate=document.createElement('div'),expected=document.createElement('div');
          candidate.setAttribute('style',(target.getAttribute('style')||'')+';'+change.key+':'+change.value+' !important;');
          expected.style.setProperty(change.key,change.value,'important');
          if(!expected.style.getPropertyValue(change.key)||candidate.style.getPropertyValue(change.key)!==expected.style.getPropertyValue(change.key))throw new Error('现有行内样式无法安全追加，请先修正该控件样式');
        }
        const patch=propertyEdits(source,descriptor,edits);
        const [revision,next]=await request('visual_save',documentRevision,encodeEdits(patch));
        if(!target.isConnected||JSON.stringify(describeElement(target))!==JSON.stringify(snapshot))throw new Error('HTML 已保存并备份，但页面发生变化，请重新加载网页核对');
        applySavedProperties(target,next,snapshot,edits);
        if(!stillSelected(version))return;
        documentRevision=revision;source=next;descriptor=describeElement(selected);
        render();status.textContent='属性已保存并备份，原有回调和脚本保持不变';position();
      }catch(error){if(stillSelected(version))fail(error);}finally{saving=false;save.disabled=false;}
    },Save);
    const undo=button('撤销',async()=>{if(saving)return;saving=true;undo.disabled=true;try{await request('text_undo');status.textContent='已撤销，正在重新加载';}catch(error){fail(error);}finally{saving=false;undo.disabled=false;}},Undo2);
    foot.prepend(save,undo);
  }
  function renderPalette() {
    const target=selected?.closest(containers)||document.body;
    const destinations=field(body,'添加位置','select','selected',{wide:true,options:[['selected','当前容器：'+labelOf(target)],['body','页面末尾']]}),palette=el('div');palette.className='visual-palette';body.append(palette);
    const items=[['button','按钮',MousePointer2],['label','标签',Type],['heading','标题',Heading2],['input','输入框',TextCursorInput],['textarea','多行输入',AlignLeft],['checkbox','复选框',CheckSquare],['radio','单选框',SquareDot],['select','下拉框',ListFilter],['container','布局容器',Square],['progress','进度条',ChartNoAxesColumn]];
    for(const [kind,name,shape]of items)palette.append(button(name,()=>addControl(kind,name,destinations.value==='body'?document.body:target,palette),shape));
  }
  function renderDescription() {
    const actions=el('div');actions.className='visual-description-actions';body.append(actions);
    actions.append(button('点选元素',startInspection,ScanSearch));
    if(!selected){body.append(el('p','未选择元素'));return;}
    const text=el('textarea');text.readOnly=true;text.className='element-description';text.setAttribute('aria-label','元素描述');body.append(text);
    const label=el('label','修改要求'),wanted=el('textarea');label.className='element-request-label';wanted.className='element-request';wanted.setAttribute('aria-label','修改要求');wanted.value=changeRequest;
    wanted.addEventListener('input',()=>changeRequest=wanted.value);label.append(wanted);body.append(label);
    try{text.value=elementDescription(selected,{source,sourcePath,sourceError});}catch(error){fail(error);return;}
    const copy=button('复制给 AI',async()=>{
      copy.disabled=true;
      try {
        const current=elementDescription(selected,{source,sourcePath,sourceError});
        text.value=current;
        await request('copy_description',current+'\n\n我的修改要求：\n'+(wanted.value.trim()||'（请补充修改要求）'));
        status.textContent='元素描述已复制';
      }catch(error){fail(error);}finally{copy.disabled=false;}
    },Copy);foot.prepend(copy);
  }
  function startInspection() {
    if(saving)return;
    disarm();++selectionVersion;inspecting=true;picking=true;window.__jadeElementPicking=true;visibleView='description';
    document.head.append(placementStyle);panel.hidden=true;outline.hidden=true;
  }
  async function addControl(kind,name,parent,palette=null) {
      if(saving)return;saving=true;if(palette)for(const b of palette.children)b.disabled=true;
      const version=selectionVersion;
      try {
        const d=describeElement(parent);
        const [revision,html]=await request('text_open');
        if(!stillSelected(version))return;
        if(JSON.stringify(describeElement(parent))!==JSON.stringify(d))throw new Error('添加位置已经改变，请重新选择');
        const liveIds=[...document.querySelectorAll('[id]')].map(n=>n.id);
        let patch=insertionEdit(html,d,kind,liveIds),available=false;
        for(let attempt=0;attempt<32;++attempt) {
          const [number]=await request('visual_name',kind,patch.value);
          if(!stillSelected(version))return;
          const checked=insertionEdit(html,d,kind,liveIds,Number(number));
          if(checked.value===number){patch=checked;available=true;break;}patch=checked;
        }
        if(!available)throw new Error('没有取得可用的控件名称');
        const [,next]=await request('visual_save',revision,encodeEdits([patch]));
        if(!parent.isConnected||JSON.stringify(describeElement(parent))!==JSON.stringify(d))throw new Error('HTML 已保存并备份，但页面发生变化，请重新加载网页核对');
        const fragment=next.slice(patch.start,patch.start+next.length-html.length);
        parent.insertAdjacentHTML('beforeend',fragment);
        if(!stillSelected(version))return;
        const control=document.getElementById(`jade_${kind}_${patch.value}`);
        saving=false;visibleView='properties';await select(control);
        if(selected===control&&isDesign()) {
          const message=name+'已添加并备份，事件模式下可生成回调';
          if(panelRequested)status.textContent=message;else notify(message,false);
        }
      }catch(error){if(stillSelected(version))fail(error);}finally{saving=false;if(palette)for(const b of palette.children)b.disabled=false;}
  }
  function render() {if(!panelRequested&&!inspecting)return;shell();if(visibleView==='description')renderDescription();else if(visibleView==='palette')renderPalette();else renderProperties();}
  async function select(element) {
    if(saving&&element!==selected)return;
    selected=element;descriptor=null;source='';sourcePath='';sourceError='源码读取中';const version=++selectionVersion;observer.disconnect();if(selected)observer.observe(selected);render();position();
    if(!selected||(!panelRequested&&!inspecting))return;
    try {
      const [revision,html,path]=await request('text_open');
      if(!stillSelected(version))return;
      documentRevision=revision;source=html;sourcePath=path||'';sourceError='';
      const d=describeElement(selected);
      resolveElement(html,d);documentRevision=revision;source=html;descriptor=d;render();
    }catch(error){if(stillSelected(version)){sourceError=error.message;if(visibleView==='description')render();else fail(error);}}
  }
  window.addEventListener('click',e=>{
    if((!isDesign()&&!picking)||window.__jadeTextEditing||isTool(e))return;
    e.preventDefault();e.stopImmediatePropagation();
    if(saving)return;
    let target=e.target instanceof Element?e.target:null;
    if(picking){disarm();visibleView='description';target=target?.closest('button,input,select,textarea,a,[role="button"]')||target;if(target)select(target);return;}
    if(armedKind) {
      const kind=armedKind;disarm();
      const parent=target?.closest(containers);if(!parent)return;
      selected=parent;descriptor=null;++selectionVersion;visibleView='properties';render();
      const name=({button:'按钮',label:'标签',heading:'标题',input:'输入框',textarea:'多行输入',checkbox:'复选框',radio:'单选框',select:'下拉框',container:'布局容器',progress:'进度条'})[kind];
      addControl(kind,name,parent);return;
    }
    if(target?.closest('button'))target=target.closest('button');
    else if(target?.closest('svg,i'))target=target.closest('label')||target;
    target=target?.closest(controls);if(target)select(target);
  },true);
  for(const name of ['pointerdown','pointerup','mousedown','mouseup','dblclick','contextmenu'])window.addEventListener(name,e=>{
    if(picking&&!isTool(e)){e.preventDefault();e.stopImmediatePropagation();}
  },true);
  return {open:()=>{disarm();panelRequested=true;inspecting=false;visibleView='properties';select(selected);},inspect:startInspection,
    arm:kind=>{
      if(saving)return;disarm();
      if(kind==='pointer')return;
      if(!['button','label','heading','input','textarea','checkbox','radio','select','container','progress'].includes(kind))return;
      armedKind=kind;document.head.append(placementStyle);panel.hidden=true;
    },deactivate:()=>{disarm();panelRequested=false;inspecting=false;++selectionVersion;outline.hidden=true;observer.disconnect();},select};
}
