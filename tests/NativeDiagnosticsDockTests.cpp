#define NOMINMAX
#define UNICODE
#define _UNICODE
#include "../src/NativeDiagnosticsDock.h"
#include "../src/NativeVisualDock.h"
#include <cstdio>
#include <cstdlib>

void Check(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
    std::printf("PASS %s\n", name);
}
std::wstring Text(HWND window) {
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    text.resize(static_cast<size_t>(GetWindowTextW(window, text.data(), static_cast<int>(text.size()))));
    return text;
}
void DoubleClick(HWND list, int row, int column) {
    NMITEMACTIVATE click{}; click.hdr.hwndFrom = list; click.hdr.code = NM_DBLCLK;
    click.iItem = row; click.iSubItem = column;
    SendMessageW(GetParent(list), WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&click));
}
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    const auto module = GetModuleHandleW(nullptr);
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Jade diagnostic test",
        WS_POPUP | WS_VISIBLE, -30000, -30000, 700, 700, nullptr, nullptr, module, nullptr);
    HWND tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE,
        0, 0, 700, 700, host, nullptr, module, nullptr);
    for (auto name : {L"支持库", L"程序", L"属性", L"AI"}) {
        TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(name);
        TabCtrl_InsertItem(tab, TabCtrl_GetItemCount(tab), &item);
    }
    NativeVisualDock visual; visual.Initialize(host, module); visual.Ensure();
    NativeDiagnosticsDock dock; int jumps = 0;
    dock.Initialize(host, module, [&](const auto& assembly, const auto& callback) {
        Check(assembly == L"用户程序集" && callback == L"测试被单击", "callback identity retained"); ++jumps;
    });
    dock.Ensure(); dock.Ensure();
    Check(dock.IsReady() && TabCtrl_GetItemCount(tab) == 5, "diagnostics tab restored exactly once");
    HWND page = FindWindowExW(host, nullptr, L"JadeDesigner.NativeDiagnosticsDock", nullptr);
    HWND list = FindWindowExW(page, nullptr, WC_LISTVIEWW, nullptr);
    HWND output = FindWindowExW(page, nullptr, L"EDIT", nullptr);
    TabCtrl_SetCurSel(tab, 4);
    Check(IsWindowVisible(page), "diagnostics tab becomes visible");
    TabCtrl_SetCurSel(tab, 2);
    Check(!IsWindowVisible(page) && visual.IsReady(), "property tab remains available");
    TabCtrl_SetCurSel(tab, 4);
    NativeDiagnosticsDock::Record row{L"1", L"测试被单击", L"app:test", std::wstring(40000, L'中'),
        L"", L"等待应答", L"用户程序集", L"0 ms"};
    dock.Add(row); row.response = L"{\"ok\":true}"; row.state = L"应答已到达"; dock.Add(row);
    Check(ListView_GetItemCount(list) == 1, "response updates original request");
    Check(Text(output).find(row.payload) != std::wstring::npos, "long JSON detail is not truncated");
    DoubleClick(list, 0, 3);
    HWND detail = FindWindowW(L"JadeDesigner.DiagnosticDetail", nullptr);
    Check(IsWindow(detail) && Text(GetDlgItem(detail, 1)).find(row.response) != std::wstring::npos &&
        Text(GetDlgItem(detail, 1)).find(row.payload) != std::wstring::npos, "double click opens complete request and response");
    DoubleClick(list, 0, 1); Check(jumps == 1, "callback double click still locates code");
    const auto first = row;
    row.payload = L"{\"latest\":true}"; row.response.clear();
    for (int i = 2; i <= 5; ++i) { row.id = std::to_wstring(i); row.state = L"等待应答"; dock.Add(row); }
    Check(ListView_GetItemCount(list) == 1, "repeated callback keeps exactly one row");
    Check(Text(output).find(row.payload) != std::wstring::npos && Text(output).find(first.response) == std::wstring::npos,
        "new request replaces parameters and clears previous response");
    dock.Add(first);
    Check(Text(output).find(first.response) == std::wstring::npos, "older asynchronous response cannot overwrite current request");
    row.response = L"{\"latestReply\":true}"; row.state = L"应答已到达"; dock.Add(row);
    Check(Text(output).find(row.response) != std::wstring::npos && ListView_GetItemCount(list) == 1,
        "latest response updates the existing callback row");
    row.callback.clear(); row.channel = L"app:unnamed"; row.id = L"unnamed-1"; row.state = L"等待应答"; dock.Add(row);
    row.id = L"unnamed-2"; dock.Add(row);
    Check(ListView_GetItemCount(list) == 2, "unnamed callback groups by channel");
    row.channel = L"app:other"; row.id = L"other"; dock.Add(row);
    Check(ListView_GetItemCount(list) == 3, "distinct unnamed channels remain separate");
    for (int i = 6; i <= 210; ++i) {
        row.id = std::to_wstring(i); row.callback = L"回调" + row.id; row.state = L"等待应答"; dock.Add(row);
    }
    Check(ListView_GetItemCount(list) == 200, "diagnostic history is bounded");
    row.id = L"1"; row.state = L"应答已到达"; dock.Add(row);
    Check(ListView_GetItemCount(list) == 200, "late response does not resurrect evicted request");
    dock.Shutdown();
    Check(TabCtrl_GetItemCount(tab) == 4 && TabCtrl_GetCurSel(tab) == 2 && !IsWindow(detail) && visual.IsReady(), "shutdown restores native tab and removes only diagnostic-owned UI");
    visual.Shutdown(); DestroyWindow(host);
}
