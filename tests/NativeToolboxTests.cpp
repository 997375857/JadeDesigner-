#include "../src/NativeToolbox.h"
#include <cstdio>
#include <cstdlib>

static int checks = 0;
void Check(bool value, const char* name) {
    if (!value) { std::printf("FAIL %s\n", name); std::exit(1); }
    ++checks; std::printf("PASS %s\n", name);
}
struct Fixture {
    HWND main, owner, dialog, basic, extra, tree, hidden;
    Fixture() {
        const auto module=GetModuleHandleW(nullptr);
        main=CreateWindowExW(0,L"STATIC",L"Toolbox fixture",WS_OVERLAPPEDWINDOW,-31000,-31000,320,600,nullptr,nullptr,module,nullptr);
        owner=CreateWindowExW(0,L"STATIC",L"窗口组件箱",WS_CHILD|WS_VISIBLE,0,0,300,550,main,reinterpret_cast<HMENU>(110),module,nullptr);
        dialog=CreateWindowExW(0,L"#32770",L"",WS_CHILD|WS_VISIBLE,0,0,280,530,owner,nullptr,module,nullptr);
        basic=CreateWindowExW(0,L"BUTTON",L"基本组件",WS_CHILD|WS_VISIBLE,0,0,280,24,dialog,reinterpret_cast<HMENU>(11225),module,nullptr);
        extra=CreateWindowExW(0,L"BUTTON",L"扩展组件",WS_CHILD|WS_VISIBLE,0,500,280,24,dialog,reinterpret_cast<HMENU>(11226),module,nullptr);
        tree=CreateWindowExW(0,WC_TREEVIEWW,L"Tree1",WS_CHILD|WS_VISIBLE,0,24,280,476,dialog,reinterpret_cast<HMENU>(1225),module,nullptr);
        hidden=CreateWindowExW(0,L"BUTTON",L"hidden",WS_CHILD,0,0,0,0,dialog,reinterpret_cast<HMENU>(16225),module,nullptr);
        ShowWindow(main,SW_SHOWNOACTIVATE);
    }
    ~Fixture(){DestroyWindow(main);}
};
int main() {
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_TREEVIEW_CLASSES};InitCommonControlsEx(&cc);
    Fixture f;NativeToolbox box;std::wstring picked,error;int toggles=0;
    box.Initialize(f.main,GetModuleHandleW(nullptr),[&]{++toggles;ShowWindow(f.owner,IsWindowVisible(f.owner)?SW_HIDE:SW_SHOWNOACTIVATE);},[&](const std::wstring& kind){picked=kind;});
    Check(box.Configure(true,true,error),"known component dialog accepted");
    const auto panel=FindWindowExW(f.dialog,nullptr,L"JadeDesigner.NativeToolbox",nullptr);
    const auto list=FindWindowExW(panel,nullptr,L"LISTBOX",nullptr);
    Check(panel&&list&&IsWindowVisible(panel),"own native palette is visible");
    Check(!IsWindowVisible(f.tree)&&!IsWindowVisible(f.basic)&&!IsWindowVisible(f.extra),"original widgets hidden but retained");
    Check(SendMessageW(list,LB_GETCOUNT,0,0)==11,"pointer and ten controls");
    SendMessageW(list,LB_SETCURSEL,1,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(1,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
    Check(picked==L"button","selection routes only through owned list");
    SetWindowPos(f.dialog,nullptr,0,0,180,350,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT rect{};GetClientRect(panel,&rect);Check(rect.right==180&&rect.bottom==350,"palette follows native docking size");
    box.Pointer();Check(SendMessageW(list,LB_GETCURSEL,0,0)==0,"completed placement resets pointer");
    box.SetEditable(false);box.Configure(true,true,error);box.Update(true);
    Check(IsWindowVisible(panel)&&!IsWindowVisible(f.tree)&&!IsWindowEnabled(list),"event and preview modes keep Jade palette but disable placement");
    picked=L"pointer";
    SendMessageW(list,LB_SETCURSEL,1,0);SendMessageW(panel,WM_COMMAND,MAKEWPARAM(1,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
    Check(picked==L"pointer","disabled palette cannot arm a control");
    box.SetEditable(true);box.Configure(true,true,error);
    Check(IsWindowEnabled(list)&&FindWindowExW(f.dialog,nullptr,L"JadeDesigner.NativeToolbox",nullptr)==panel,"design mode reuses the same palette");
    box.Configure(false,false,error);
    Check(picked==L"pointer","leaving the native palette cancels pending webpage placement");
    Check(!IsWindow(panel)&&IsWindowVisible(f.tree)&&IsWindowVisible(f.basic)&&IsWindowVisible(f.extra),"disable restores original widgets");
    Check(!(GetWindowLongPtrW(f.hidden,GWL_STYLE)&WS_VISIBLE)&&toggles==0,"hidden original children and original dock visibility preserved");
    ShowWindow(f.owner,SW_HIDE);Check(box.Configure(true,true,error)&&toggles==1,"public command opens hidden component bar");
    box.Update(false);Check(!IsWindowVisible(f.owner)&&toggles==2,"leaving preview document restores originally hidden bar");
    box.Update(true);Check(IsWindowVisible(f.owner)&&toggles==3,"returning to preview document reacquires the bar");
    ShowWindow(f.owner,SW_HIDE);box.Update(true);
    Check(picked==L"unavailable"&&toggles==3,"user closing bar is respected without reopening it");
    ShowWindow(f.owner,SW_SHOWNOACTIVATE);SetWindowTextW(f.basic,L"Other");
    Check(box.Configure(true,true,error)&&box.IsFloating()&&IsWindowVisible(f.tree),"unrecognized native pane uses independent palette without changing it");
    box.Configure(false,false,error);
    SetWindowTextW(f.basic,L"基本组件");
    Fixture duplicate;SetParent(duplicate.owner,f.main);
    Check(box.Configure(true,true,error)&&box.IsFloating()&&IsWindowVisible(f.tree),"ambiguous native panes remain untouched with standalone palette");
    box.Configure(false,false,error);
    SetParent(duplicate.owner,duplicate.main);
    Check(box.Configure(true,true,error),"can reenable after a refused target");
    DestroyWindow(f.owner);box.Update(false);box.Shutdown();
    Check(true,"parent destruction and repeated shutdown are safe");
    box.Initialize(f.main,GetModuleHandleW(nullptr),[]{},[&](const std::wstring& kind){picked=kind;});
    Check(box.Configure(true,true,error)&&box.IsFloating(),"windowless project gets independent toolbox");
    const auto floating=FindWindowW(L"JadeDesigner.NativeToolbox",L"Jade 网页组件（独立）");
    const auto floatingList=FindWindowExW(floating,nullptr,L"LISTBOX",nullptr);
    Check(floating&&GetWindow(floating,GW_OWNER)==f.main&&SendMessageW(floatingList,LB_GETCOUNT,0,0)==11,"standalone toolbox owned by IDE with full palette");
    box.SetEditable(true);SendMessageW(floatingList,LB_SETCURSEL,1,0);SendMessageW(floating,WM_COMMAND,MAKEWPARAM(1,LBN_SELCHANGE),reinterpret_cast<LPARAM>(floatingList));
    Check(picked==L"button","standalone selection arms webpage control");
    box.SetEditable(false);box.Update(true);Check(IsWindow(floating)&&!IsWindowEnabled(floatingList),"standalone persists read-only outside design");
    SetWindowPos(floating,nullptr,0,0,260,320,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    RECT client{},listRect{};GetClientRect(floating,&client);GetClientRect(floatingList,&listRect);
    Check(listRect.right>180&&listRect.right<client.right,"standalone resize updates child list");
    box.Update(false);Check(!IsWindow(floating),"leaving webpage closes independent palette");
    box.Update(true);Check(box.IsFloating(),"returning to webpage recreates independent palette");
    SendMessageW(FindWindowW(L"JadeDesigner.NativeToolbox",L"Jade 网页组件（独立）"),WM_CLOSE,0,0);box.Update(true);
    Check(picked==L"unavailable"&&!box.IsFloating(),"closing standalone palette cancels choice without reopening");
    Fixture collapsed;ShowWindow(collapsed.owner,SW_HIDE);box.Shutdown();
    box.Initialize(collapsed.main,GetModuleHandleW(nullptr),[]{},[&](const std::wstring& kind){picked=kind;});
    Check(box.Configure(true,true,error)&&box.IsFloating()&&!IsWindowVisible(collapsed.owner),"native bar that cannot expand is no longer a blocker");
    box.Shutdown();Check(!(GetWindowLongPtrW(collapsed.owner,GWL_STYLE)&WS_VISIBLE),"standalone leaves unavailable native bar unchanged");
    std::printf("%d native toolbox checks passed\n",checks);
}
