#pragma once

#include <Windows.h>
#include <CommCtrl.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>

// A small native page hosted by e5.95's existing 工作夹. It intentionally
// owns only the added tab and child controls; existing tool pages are untouched.
class NativeDiagnosticsDock {
public:
    using JumpCallback = std::function<void(const std::wstring&, const std::wstring&)>;
    void Initialize(HWND main, HINSTANCE module, JumpCallback jump = {}) {
        main_ = main; module_ = module; jump_ = std::move(jump);
    }

    void Ensure() {
        if (page_ || !IsWindow(main_)) return;
        FindHost();
        if (!tab_ || !tabParent_) return;
        TCITEMW item{};
        wchar_t title[] = L"通信诊断";
        item.mask = TCIF_TEXT;
        item.pszText = title;
        const LRESULT count = SendMessageW(tab_, TCM_GETITEMCOUNT, 0, 0);
        if (count < 0) return;
        tabIndex_ = static_cast<int>(SendMessageW(tab_, TCM_INSERTITEMW, count,
            reinterpret_cast<LPARAM>(&item)));
        if (tabIndex_ < 0) return;

        RegisterPageClass();
        page_ = CreateWindowExW(WS_EX_CONTROLPARENT, pageClass_, L"通信诊断",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, tabParent_, nullptr,
            module_, this);
        if (!page_) {
            SendMessageW(tab_, TCM_DELETEITEM, tabIndex_, 0);
            tabIndex_ = -1;
            return;
        }
        title_ = CreateWindowExW(0, L"STATIC", L"通信诊断",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 8, 4, 180, 28,
            page_, nullptr, module_, nullptr);
        list_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            4, 34, 100, 100, page_, nullptr, module_, nullptr);
        outputLabel_ = CreateWindowExW(0, L"STATIC", L"当前易语言参数 JSON（双击表格参数可复制）",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 8, 0, 420, 28,
            page_, nullptr, module_, nullptr);
        output_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            4, 0, 100, 60, page_, nullptr, module_, nullptr);
        if (!title_ || !list_ || !outputLabel_ || !output_) { DestroyPage(); return; }
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        AddColumn(0, L"序号", 48);
        AddColumn(1, L"回调", 190);
        AddColumn(2, L"易语言参数 JSON", 420);
        SetWindowSubclass(tabParent_, ParentProc, subclassId_, reinterpret_cast<DWORD_PTR>(this));
        SetWindowSubclass(tab_, TabProc, tabSubclassId_, reinterpret_cast<DWORD_PTR>(this));
        Layout();
        UpdateVisibility();
    }

    void Layout() {
        if (!page_ || !tabParent_ || !tab_) return;
        // The host tab control covers the whole 工作夹, while its tab strip
        // is drawn at the bottom. Its window rectangle is therefore not the
        // content rectangle. Let the common-controls implementation calculate
        // the display area for both top and bottom tab styles.
        RECT content{};
        GetClientRect(tab_, &content);
        TabCtrl_AdjustRect(tab_, FALSE, &content);
        MapWindowPoints(tab_, tabParent_, reinterpret_cast<POINT*>(&content), 2);
        const int left = static_cast<int>(content.left);
        const int top = static_cast<int>(content.top);
        const int width = std::max(0, static_cast<int>(content.right - content.left));
        const int height = std::max(0, static_cast<int>(content.bottom - content.top));
        SetWindowPos(page_, HWND_TOP, left, top, width, height, SWP_NOACTIVATE);
        MoveWindow(title_, 8, 4, std::max(0, width - 16), 28, TRUE);
        const int outputHeight = std::min(116, std::max(76, height / 4));
        const int listHeight = std::max(48, height - 42 - outputHeight);
        MoveWindow(list_, 4, 34, std::max(0, width - 8), listHeight, TRUE);
        MoveWindow(outputLabel_, 8, 38 + listHeight, std::max(0, width - 16), 22, TRUE);
        MoveWindow(output_, 4, 60 + listHeight, std::max(0, width - 8), outputHeight - 22, TRUE);
        for (int column = 0; column < 3; ++column) {
            const int desired = column == 2 ? std::max(260, width - 48 - std::max(150, width / 5))
                : column == 1 ? std::max(150, width / 5) : 48;
            ListView_SetColumnWidth(list_, column, desired);
        }
    }

    void Add(std::wstring callback, std::wstring payload, std::wstring assembly = {}) {
        Ensure();
        if (!list_) return;
        payload = NormalizePayload(payload);
        const auto found = callbackRows_.find(callback);
        int row = found == callbackRows_.end() ? -1 : found->second;
        if (row < 0) {
            row = ListView_GetItemCount(list_);
            LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = row;
            const std::wstring number = std::to_wstring(row + 1);
            item.pszText = const_cast<wchar_t*>(number.c_str());
            const int inserted = static_cast<int>(SendMessageW(list_, LVM_INSERTITEMW, 0,
                reinterpret_cast<LPARAM>(&item)));
            if (inserted < 0) return;
            callbackRows_[callback] = inserted;
            SetText(inserted, 1, callback);
            callbackAssemblies_[callback] = std::move(assembly);
        }
        // Some UI events legitimately carry an empty argument. Do not erase a
        // previously captured JSON just because the repeated callback has no
        // new payload.
        if (!payload.empty()) SetText(row, 2, payload);
        latestRow_ = row;
        if (output_ && !payload.empty()) SetWindowTextW(output_, payload.c_str());
        InvalidateRect(list_, nullptr, FALSE);
        while (ListView_GetItemCount(list_) > 200) ListView_DeleteItem(list_, 0);
        RebuildRowIndex();
        row = callbackRows_[callback];
        ListView_EnsureVisible(list_, row, FALSE);
    }

    void Shutdown() {
        if (tab_) RemoveWindowSubclass(tab_, TabProc, tabSubclassId_);
        if (tabParent_) RemoveWindowSubclass(tabParent_, ParentProc, subclassId_);
        DestroyPage();
        callbackRows_.clear();
        callbackAssemblies_.clear();
        latestRow_ = -1;
        main_ = tab_ = tabParent_ = nullptr;
    }

private:
    inline static constexpr wchar_t pageClass_[] = L"JadeDesigner.NativeDiagnosticsDock";
    inline static constexpr UINT_PTR subclassId_ = 0x4A440030;
    inline static constexpr UINT_PTR tabSubclassId_ = 0x4A440031;
    HWND main_ = nullptr, tabParent_ = nullptr, tab_ = nullptr;
    HWND page_ = nullptr, title_ = nullptr, list_ = nullptr;
    HWND outputLabel_ = nullptr, output_ = nullptr;
    HINSTANCE module_ = nullptr;
    int tabIndex_ = -1;
    bool registered_ = false;
    std::map<std::wstring, int> callbackRows_;
    std::map<std::wstring, std::wstring> callbackAssemblies_;
    int latestRow_ = -1;
    JumpCallback jump_;

    static bool IsClass(HWND window, const wchar_t* expected) {
        wchar_t name[128]{}; GetClassNameW(window, name, 128);
        return _wcsicmp(name, expected) == 0;
    }
    static bool HasText(HWND window, const wchar_t* expected) {
        wchar_t text[128]{}; GetWindowTextW(window, text, 128);
        return wcscmp(text, expected) == 0;
    }
    struct Search { HWND work = nullptr, page = nullptr, tab = nullptr; };
    static BOOL CALLBACK FindProc(HWND window, LPARAM value) {
        auto& search = *reinterpret_cast<Search*>(value);
        if (!search.work && IsClass(window, L"AfxControlBar42s") && HasText(window, L"工作夹")) {
            search.work = window;
            return TRUE;
        }
        return TRUE;
    }
    static BOOL CALLBACK FindTabProc(HWND window, LPARAM value) {
        auto& search = *reinterpret_cast<Search*>(value);
        if (!search.page && IsClass(window, L"#32770") && IsWindowVisible(window)) search.page = window;
        if (search.page && IsClass(window, WC_TABCONTROLW) && IsWindowVisible(window) &&
            GetParent(window) == search.page) search.tab = window;
        return TRUE;
    }
    void FindHost() {
        Search search{};
        EnumChildWindows(main_, FindProc, reinterpret_cast<LPARAM>(&search));
        if (!search.work) return;
        EnumChildWindows(search.work, FindTabProc, reinterpret_cast<LPARAM>(&search));
        tabParent_ = search.page; tab_ = search.tab;
    }
    void RegisterPageClass() {
        if (registered_) return;
        WNDCLASSW wc{}; wc.hInstance = module_; wc.lpfnWndProc = PageProc;
        wc.lpszClassName = pageClass_; wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        registered_ = RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    static LRESULT CALLBACK PageProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_NCCREATE) SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        if (message == WM_NOTIFY) {
            auto* self = reinterpret_cast<NativeDiagnosticsDock*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            const auto* header = reinterpret_cast<const NMHDR*>(lp);
            if (self && header && header->hwndFrom == self->list_) {
                if (header->code == NM_CLICK) self->CopyJsonAtCursor();
                if (header->code == NM_DBLCLK) self->JumpAtCursor();
                if (header->code == NM_CUSTOMDRAW) {
                    const auto* draw = reinterpret_cast<const NMLVCUSTOMDRAW*>(lp);
                    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
                        return CDRF_NOTIFYSUBITEMDRAW;
                    if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                        if (static_cast<int>(draw->nmcd.dwItemSpec) == self->latestRow_)
                            const_cast<NMLVCUSTOMDRAW*>(draw)->clrText = RGB(190, 0, 0);
                        return CDRF_DODEFAULT;
                    }
                }
            }
        }
        if (message == WM_ERASEBKGND) return 1;
        return DefWindowProcW(window, message, wp, lp);
    }
    static LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wp, LPARAM lp,
        UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<NativeDiagnosticsDock*>(data);
        const LRESULT result = DefSubclassProc(window, message, wp, lp);
        if (message == WM_SIZE || message == WM_WINDOWPOSCHANGED) self.Layout();
        if (message == WM_NOTIFY) {
            const auto* header = reinterpret_cast<const NMHDR*>(lp);
            if (header && header->hwndFrom == self.tab_ && header->code == TCN_SELCHANGE) self.UpdateVisibility();
        }
        return result;
    }
    static LRESULT CALLBACK TabProc(HWND window, UINT message, WPARAM wp, LPARAM lp,
        UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<NativeDiagnosticsDock*>(data);
        const LRESULT result = DefSubclassProc(window, message, wp, lp);
        // Some e5.95 builds consume the tab notification inside the native
        // control, so the parent subclass never sees TCN_SELCHANGE. Refresh
        // after user navigation at the control itself as a reliable fallback.
        if (message == WM_LBUTTONUP || message == WM_KEYUP || message == WM_MBUTTONUP)
            self.UpdateVisibility();
        return result;
    }
    void UpdateVisibility() {
        if (!tab_ || !page_) return;
        const int selected = static_cast<int>(SendMessageW(tab_, TCM_GETCURSEL, 0, 0));
        ShowWindow(page_, selected == tabIndex_ ? SW_SHOW : SW_HIDE);
    }
    void AddColumn(int index, const wchar_t* title, int width) {
        LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t*>(title); column.cx = width;
        SendMessageW(list_, LVM_INSERTCOLUMNW, index, reinterpret_cast<LPARAM>(&column));
    }
    void SetText(int row, int column, const std::wstring& text) {
        LVITEMW item{}; item.iSubItem = column;
        item.pszText = const_cast<wchar_t*>(text.c_str());
        SendMessageW(list_, LVM_SETITEMTEXTW, row, reinterpret_cast<LPARAM>(&item));
    }
    void RebuildRowIndex() {
        callbackRows_.clear();
        wchar_t callback[512]{};
        for (int row = 0; row < ListView_GetItemCount(list_); ++row) {
            LVITEMW item{}; item.iSubItem = 1; item.pszText = callback;
            item.cchTextMax = ARRAYSIZE(callback); callback[0] = L'\0';
            SendMessageW(list_, LVM_GETITEMTEXTW, row, reinterpret_cast<LPARAM>(&item));
            callbackRows_[callback] = row;
        }
        if (latestRow_ >= ListView_GetItemCount(list_)) latestRow_ = ListView_GetItemCount(list_) - 1;
    }
    static std::wstring NormalizePayload(const std::wstring& value) {
        if (value.size() < 2 || value.front() != L'"' || value.back() != L'"') return value;
        std::wstring result;
        result.reserve(value.size() - 2);
        for (size_t i = 1; i + 1 < value.size(); ++i) {
            if (value[i] != L'\\' || i + 1 >= value.size() - 1) { result += value[i]; continue; }
            const wchar_t escaped = value[++i];
            if (escaped == L'"' || escaped == L'\\' || escaped == L'/') result += escaped;
            else if (escaped == L'n') result += L'\n';
            else if (escaped == L'r') result += L'\r';
            else if (escaped == L't') result += L'\t';
            else if (escaped == L'u' && i + 4 < value.size() - 1) {
                unsigned int code = 0;
                for (int digit = 0; digit < 4; ++digit) {
                    const wchar_t c = value[++i];
                    code = code * 16 + (c >= L'0' && c <= L'9' ? c - L'0' :
                        c >= L'a' && c <= L'f' ? c - L'a' + 10 : c - L'A' + 10);
                }
                result += static_cast<wchar_t>(code);
            } else result += escaped;
        }
        return result;
    }
    void CopyJsonAtCursor() {
        if (!list_) return;
        POINT point{}; GetCursorPos(&point); ScreenToClient(list_, &point);
        LVHITTESTINFO hit{}; hit.pt = point;
        const int row = ListView_SubItemHitTest(list_, &hit);
        if (row < 0 || hit.iSubItem != 2) return;
        wchar_t buffer[32768]{}; LVITEMW item{}; item.iSubItem = 2;
        item.pszText = buffer; item.cchTextMax = ARRAYSIZE(buffer);
        SendMessageW(list_, LVM_GETITEMTEXTW, row, reinterpret_cast<LPARAM>(&item));
        if (!OpenClipboard(list_)) return;
        EmptyClipboard();
        const size_t bytes = (wcslen(buffer) + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory) {
            memcpy(GlobalLock(memory), buffer, bytes);
            GlobalUnlock(memory); SetClipboardData(CF_UNICODETEXT, memory);
        }
        CloseClipboard();
        if (output_) SetWindowTextW(output_, buffer);
    }
    void JumpAtCursor() {
        if (!jump_ || !list_) return;
        POINT point{}; GetCursorPos(&point); ScreenToClient(list_, &point);
        LVHITTESTINFO hit{}; hit.pt = point;
        const int row = ListView_SubItemHitTest(list_, &hit);
        if (row < 0 || hit.iSubItem != 1) return;
        wchar_t callback[512]{}; LVITEMW item{}; item.iSubItem = 1;
        item.pszText = callback; item.cchTextMax = ARRAYSIZE(callback);
        SendMessageW(list_, LVM_GETITEMTEXTW, row, reinterpret_cast<LPARAM>(&item));
        const std::wstring name(callback);
        const auto found = callbackAssemblies_.find(name);
        jump_(found == callbackAssemblies_.end() ? L"" : found->second, name);
        // The IDE's native jump command selects the 程序 tab. Restore this
        // diagnostics tab after the caret has been moved to the callback.
        if (tab_ && tabIndex_ >= 0) {
            SendMessageW(tab_, TCM_SETCURSEL, tabIndex_, 0);
            UpdateVisibility();
        }
    }
    void DestroyPage() {
        if (page_ && IsWindow(page_)) DestroyWindow(page_);
        page_ = title_ = list_ = outputLabel_ = output_ = nullptr;
        if (tab_ && tabIndex_ >= 0) SendMessageW(tab_, TCM_DELETEITEM, tabIndex_, 0);
        tabIndex_ = -1;
    }
};
