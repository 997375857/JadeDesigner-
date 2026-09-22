#define NOMINMAX
#define UNICODE
#define _UNICODE
#include "../src/NativeVisualDock.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool value,const char* name) {
    if(!value){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}
    ++checks;std::printf("PASS %s\n",name);
}
std::wstring Cell(HWND list,int row,int column) {
    wchar_t buffer[256]{};LVITEMW item{};item.iSubItem=column;item.pszText=buffer;item.cchTextMax=256;
    SendMessageW(list,LVM_GETITEMTEXTW,row,reinterpret_cast<LPARAM>(&item));return buffer;
}
void DoubleClick(HWND list,int row) {
    NMITEMACTIVATE click{};click.hdr.hwndFrom=list;click.hdr.code=NM_DBLCLK;click.iItem=row;
    SendMessageW(GetParent(list),WM_NOTIFY,0,reinterpret_cast<LPARAM>(&click));
}
}
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES|ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
    const auto module=GetModuleHandleW(nullptr);
    // IsWindowVisible requires WS_VISIBLE; keep this isolated test host offscreen.
    HWND host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Jade isolated UI test",
        WS_POPUP|WS_VISIBLE,-30000,-30000,700,700,nullptr,nullptr,module,nullptr);
    HWND tab=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE,0,0,700,700,host,nullptr,module,nullptr);
    for(auto text:{L"程序",L"属性"}){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(text);TabCtrl_InsertItem(tab,TabCtrl_GetItemCount(tab),&item);}
    NativeVisualDock dock;int callbacks=0;HWND events=nullptr;bool reenter=false;
    NativeVisualDock::Selection selection;selection.id=L"主播列表";selection.type=L"super-list";selection.documentGeneration=7;
    dock.Initialize(host,module,{},[&](const auto& picked,const auto& label,const auto& code){
        ++callbacks;
        Check(picked.id==L"主播列表"&&label==L"被双击"&&code=="dblclick","native double click carries selected event");
        TabCtrl_SetCurSel(tab,0);
        if(reenter){dock.SetSelection(selection);DoubleClick(events,1);}
        return L"已定位";
    });
    dock.Ensure();Check(dock.IsReady(),"property dock attaches to existing tab");
    HWND page=FindWindowExW(host,nullptr,L"JadeDesigner.NativeVisualDock",nullptr);
    for(HWND child=FindWindowExW(page,nullptr,WC_LISTVIEWW,nullptr);child;child=FindWindowExW(page,child,WC_LISTVIEWW,nullptr)) {
        wchar_t text[128]{};LVCOLUMNW column{};column.mask=LVCF_TEXT;column.pszText=text;column.cchTextMax=128;
        SendMessageW(child,LVM_GETCOLUMNW,0,reinterpret_cast<LPARAM>(&column));
        if(std::wstring(text)==L"可视化事件")events=child;
    }
    Check(events!=nullptr,"native event table is present");
    dock.SetSelection(selection);
    Check(ListView_GetItemCount(events)==9,"native event table uses all module list events");
    Check(Cell(events,0,1)==L"预览模式只读","event table defaults to read-only");
    DoubleClick(events,1);Check(callbacks==0,"read-only native double click does not route");
    dock.SetEventsEnabled(true);reenter=true;DoubleClick(events,1);
    Check(callbacks==1,"message reentry cannot route duplicate event");
    Check(TabCtrl_GetCurSel(tab)==1,"event navigation preserves property tab");
    dock.SetEventsEnabled(false);DoubleClick(events,1);Check(callbacks==1,"switching back to preview disables native writes");
    dock.Clear();dock.SetEventsEnabled(true);DoubleClick(events,1);
    Check(callbacks==1&&ListView_GetItemCount(events)==1,"navigation clears stale selection and events");
    selection.type=L"button";dock.SetSelection(selection);
    Check(ListView_GetItemCount(events)==1&&Cell(events,0,0)==L"被单击","unsupported button double click is absent");
    selection.type=L"checkbox";dock.SetSelection(selection);
    Check(ListView_GetItemCount(events)==1&&Cell(events,0,1).find(L"没有已公开")!=std::wstring::npos,"deferred checkbox events stay hidden");
    dock.Shutdown();DestroyWindow(host);
    std::printf("%d native property checks passed\n",checks);
}
