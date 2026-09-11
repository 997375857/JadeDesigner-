import {createElement,Activity,X,Trash2} from 'lucide';
import {createTrace,wrapInvoke} from './diagnostics-core.js';

if(window===window.top && !window.__jadeRuntimeDiagnostics) {
  const trace=createTrace();
  const state={trace,installed:false,reason:'等待 JadeView 接口'};
  Object.defineProperty(window,'__jadeRuntimeDiagnostics',{value:state});
  let wrapped;
  const attach=()=>{
    if(state.installed)return;
    const jade=window.jade;
    if(!jade||typeof jade.invoke!=='function')return;
    const original=jade.invoke;
    try{
      wrapped=wrapInvoke(original,trace,()=>!!window.__jadeDesignerPreview);
      jade.invoke=wrapped;
      state.installed=jade.invoke===wrapped;
      state.reason=state.installed?'':'接口不可写，未接入';
    }catch{state.reason='接口不可写，未接入';}
  };
  attach();
  const mount=()=>{
    attach();
    const host=document.createElement('jade-runtime-diagnostics');
    host.style.cssText='all:initial;position:fixed;left:12px;bottom:12px;z-index:2147482900;font:14px/1.5 "Microsoft YaHei",sans-serif;';
    const root=host.attachShadow({mode:'open'});
    const node=(tag,text)=>{const n=document.createElement(tag);if(text)n.textContent=text;return n;};
    const style=node('style');style.textContent=`
      *{box-sizing:border-box;letter-spacing:0}button{font:inherit;background:white;color:#25332d;border:1px solid #aab9b1;border-radius:5px;padding:8px;cursor:pointer}svg{width:18px;height:18px;display:block}
      section{position:fixed;left:12px;bottom:60px;width:min(800px,calc(100vw - 24px));height:min(500px,calc(100vh - 84px));background:#fff;color:#25332d;border:1px solid #aab9b1;border-radius:6px;box-shadow:0 5px 24px #0003;display:flex;flex-direction:column}section[hidden]{display:none}
      header{display:flex;align-items:center;gap:8px;padding:10px;border-bottom:1px solid #dbe3de}strong{flex:1}p{margin:8px 10px;font-size:12px;overflow-wrap:anywhere}main{overflow:auto;flex:1;min-height:0}table{border-collapse:collapse;width:100%;min-width:520px;font-size:13px}th,td{padding:7px;text-align:left;border-bottom:1px solid #e1e6e3;overflow-wrap:anywhere}th{position:sticky;top:0;background:#f0f5f2}
    `;
    const panel=node('section');panel.hidden=true;panel.setAttribute('role','dialog');panel.setAttribute('aria-label','运行通信诊断');
    const icon=(shape,title,fn)=>{const b=node('button');b.title=title;b.setAttribute('aria-label',title);b.append(createElement(shape));b.onclick=fn;return b;};
    const head=node('header'),label=node('strong','运行通信诊断'),summary=node('p'),main=node('main'),table=node('table'),thead=node('thead'),headrow=node('tr'),tbody=node('tbody');
    for(const name of ['序号','频道','结果','耗时'])headrow.append(node('th',name));
    thead.append(headrow);table.append(thead,tbody);main.append(table);
    const render=()=>{
      const preview=!!window.__jadeDesignerPreview;
      summary.textContent=preview?'当前为设计器预览；模拟应答不代表业务执行。':!state.installed?state.reason:window.jade?.invoke!==wrapped?'接口已被其他代码替换，后续调用未采集。':'已接入网页端；主进程收到、回调内部及业务完成未单独核验。不采集参数和应答正文。';
      tbody.replaceChildren();
      const names={pending:'等待应答',response:'应答到达（非业务成功判定）',rejected:'拒绝或超时',threw:'调用同步异常',unverified:'非 Promise 返回，未核验'};
      for(const r of trace.rows.slice().reverse()) {
        const tr=node('tr');
        for(const value of [r.id,r.channel,(r.mode==='preview'?'预览模拟 · ':'')+names[r.state],Math.round(r.state==='pending'?performance.now()-r.started:r.elapsed)+' ms'])tr.append(node('td',String(value)));
        tbody.append(tr);
      }
    };
    head.append(label,icon(Trash2,'清空诊断记录',()=>{trace.clear();render();}),icon(X,'关闭诊断',()=>panel.hidden=true));
    panel.append(head,summary,main);
    root.append(style,icon(Activity,'运行通信诊断',()=>{attach();panel.hidden=!panel.hidden;render();}),panel);
    for(const name of ['click','change','dblclick','pointerdown','pointerup'])host.addEventListener(name,e=>e.stopPropagation());
    document.documentElement.append(host);
    let attempts=0;
    const timer=setInterval(()=>{
      if(!host.isConnected){clearInterval(timer);return;}
      if(!state.installed&&++attempts<10)attach();
      if(!state.installed&&attempts===10&&state.reason==='等待 JadeView 接口')state.reason='未发现 JadeView 接口；打开面板时会再检查';
      if(!panel.hidden)render();
    },500);
    window.addEventListener('pagehide',()=>clearInterval(timer),{once:true});
  };
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',mount,{once:true});else mount();
}
