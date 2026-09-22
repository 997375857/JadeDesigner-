#include <Windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <cstdio>
#include <string>
#include "../src/DesignWorkspaceScript.h"
#include "../src/DesignNavigation.h"
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv)
{
    if(argc!=2)return 2;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HWND host=CreateWindowExW(0,L"STATIC",L"Jade design smoke",WS_OVERLAPPEDWINDOW,0,0,1200,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    bool done=false,shellReady=false;int code=1;
    const auto html=std::wstring(L"<!doctype html><html><head><meta charset=\"utf-8\"><meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline' https://jade-design-assets.invalid; img-src data: blob: https://jade-design-assets.invalid; font-src data: https://jade-design-assets.invalid; frame-src 'self' about:;\"></head><body><script>window.__jadeDesignToken='smoke';window.errors=[];window.onerror=function(m){window.errors.push(m)};")+DesignWorkspaceScript()+L"</script></body></html>";
    const auto expected=DesignNavigation::InlineUri(html);
    if(expected.empty()||DesignNavigation::Allow(L"https://example.com",expected,true,false,false)||
       DesignNavigation::Allow(expected,expected,false,false,false)||
       DesignNavigation::Allow(expected,expected,true,true,false)||
       DesignNavigation::Allow(expected,expected,true,false,true))return 2;
    bool pending=true;
    const auto hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,L"DesignWebViewSmoke-profile",nullptr,
      Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&](HRESULT result,ICoreWebView2Environment* env)->HRESULT{
        if(FAILED(result)||!env){done=true;return S_OK;}
        return env->CreateCoreWebView2Controller(host,Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([&](HRESULT r,ICoreWebView2Controller* c)->HRESULT{
          if(FAILED(r)||!c){done=true;return S_OK;}controller=c;c->get_CoreWebView2(&web);c->put_Bounds({0,0,1200,800});
          ComPtr<ICoreWebView2_3> resources;
          if(FAILED(web.As(&resources))||FAILED(resources->SetVirtualHostNameToFolderMapping(L"jade-design-assets.invalid",argv[1],COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))){done=true;return S_OK;}
          EventRegistrationToken token{};
          web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([&](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* a)->HRESULT{
            LPWSTR uri=nullptr;a->get_Uri(&uri);std::wprintf(L"START %.160s\n",uri?uri:L"null");
            BOOL user=TRUE,redirect=TRUE;a->get_IsUserInitiated(&user);a->get_IsRedirected(&redirect);
            if(uri&&DesignNavigation::Allow(uri,expected,pending,user!=FALSE,redirect!=FALSE))pending=false;
            else a->put_Cancel(TRUE);
            CoTaskMemFree(uri);return S_OK;
          }).Get(),&token);
          web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([&](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* a)->HRESULT{
            LPWSTR msg=nullptr;a->TryGetWebMessageAsString(&msg);std::wstring wire=msg?msg:L"";CoTaskMemFree(msg);
            std::wprintf(L"MESSAGE %.180s\n",wire.c_str());
            if(wire.find(L"\tdesign_load\t")!=wire.npos)web->PostWebMessageAsString(L"JADE_TOOL_RESULT\t1\t1\t\t\tE%3A%5Csmoke.e\t");
            if(wire.find(L"\tdesign_edit_source\t")!=wire.npos){
                web->PostWebMessageAsString(L"JADE_TOOL_RESULT\t2\t1\t<html><head><link rel='stylesheet' href='theme.css'></head><body><button id='native-source'>Original UI</button><script>parent.sourceExecuted=true</script></body></html>\thttps://jade-design-assets.invalid/");
            }
            if(wire==L"SOURCE_PROBE\t1"){if(shellReady)code=0;done=true;}
            if(wire==L"SOURCE_PROBE\t0"){done=true;}
            return S_OK;
          }).Get(),&token);
          web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([&](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* a)->HRESULT{
            BOOL ok=FALSE;COREWEBVIEW2_WEB_ERROR_STATUS error{};a->get_IsSuccess(&ok);a->get_WebErrorStatus(&error);std::printf("COMPLETE %d %d\n",ok,error);
            web->ExecuteScript(L"JSON.stringify({url:location.href,components:!!document.querySelector('#panel-body button'),canvas:!!document.querySelector('#canvas'),errors:window.errors})",
              Callback<ICoreWebView2ExecuteScriptCompletedHandler>([&](HRESULT h,LPCWSTR json)->HRESULT{
                std::wprintf(L"PROBE %08X %s\n",h,json?json:L"null");
                if(SUCCEEDED(h)&&json&&wcsstr(json,L"components\\\":true")&&
                   wcsstr(json,L"canvas\\\":true")&&wcsstr(json,L"errors\\\":[]"))shellReady=true;
                if(!shellReady){done=true;return S_OK;}
                web->ExecuteScript(L"document.querySelector('#tools button[aria-label=\"编辑现有网页\"]').click();let tries=0;const timer=setInterval(()=>{const f=document.querySelector('#canvas iframe'),d=f?.contentDocument,e=d?.getElementById('native-source');if(e&&d.defaultView.getComputedStyle(e).backgroundColor==='rgb(39, 41, 45)'&&document.querySelector('#status').textContent.includes('原网页已载入画布')){clearInterval(timer);chrome.webview.postMessage('SOURCE_PROBE\\t'+(!window.sourceExecuted&&!window.errors.length?'1':'0'));}else if(++tries>160){clearInterval(timer);chrome.webview.postMessage('SOURCE_PROBE\\t0');}},50);",nullptr);
                return S_OK;
              }).Get());return S_OK;
          }).Get(),&token);
          std::printf("NAVIGATE %08X\n",web->NavigateToString(html.c_str()));return S_OK;
        }).Get());
      }).Get());
    if(FAILED(hr))done=true;
    const auto deadline=GetTickCount64()+20000;
    while(!done&&GetTickCount64()<deadline){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(10);}
    if(controller)controller->Close();web.Reset();controller.Reset();DestroyWindow(host);CoUninitialize();return code;
}
