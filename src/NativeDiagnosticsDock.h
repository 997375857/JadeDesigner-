#pragma once

#include <Windows.h>
#include <CommCtrl.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// A small native page hosted by e5.95's existing 工作夹. It intentionally
// owns only the added tab and child controls; existing tool pages are untouched.
class NativeDiagnosticsDock {
public:
    struct Record { std::wstring id, callback, channel, payload, response, state, assembly, elapsed; };
    bool IsReady() const { return IsWindow(page_) && IsWindow(tab_) && tabIndex_ >= 0; }
    using JumpCallback = std::function<void(const std::wstring&, const std::wstring&)>;
    void Initialize(HWND main, HINSTANCE module, JumpCallback jump = {}) {
        main_ = main; module_ = module; jump_ = std::move(jump);
    }

    void Ensure() {
        if (IsReady() || !IsWindow(main_)) return;
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
        lastNativeTab_ = TabCtrl_GetCurSel(tab_);

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
        outputLabel_ = CreateWindowExW(0, L"STATIC", L"单击复制，双击查看完整内容；双击回调可定位",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 8, 0, 420, 28,
            page_, nullptr, module_, nullptr);
        output_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            4, 0, 100, 60, page_, nullptr, module_, nullptr);
        if (!title_ || !list_ || !outputLabel_ || !output_) { DestroyPage(); return; }
        SendMessageW(output_, EM_SETLIMITTEXT, 0, 0);
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        AddColumn(0, L"序号", 48);
        AddColumn(1, L"回调", 190);
        AddColumn(2, L"易语言参数 JSON", 420);
        AddColumn(3, L"返回 / 错误", 300);
        AddColumn(4, L"状态", 240);
        AddColumn(5, L"频道", 180);
        AddColumn(6, L"耗时", 70);
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

    void Add(Record record) {
        Ensure();
        if (!list_) return;
        const bool pending = record.state.find(L"等待应答") != std::wstring::npos;
        const auto found = std::find_if(rows_.begin(), rows_.end(), [&](const auto& r) {
            // A new call replaces its callback's row. Replies may update only
            // the latest request, so an older asynchronous reply cannot win.
            if (!pending) return r.id == record.id;
            if (!record.callback.empty())
                return !r.callback.empty() && _wcsicmp(r.callback.c_str(), record.callback.c_str()) == 0;
            return r.callback.empty() && r.channel == record.channel;
        });
        int row = static_cast<int>(found - rows_.begin());
        if (found == rows_.end()) {
            // A reply to an already evicted request must not displace newer data.
            if (!pending) return;
            if (rows_.size() == 200) {
                rows_.erase(rows_.begin()); ListView_DeleteItem(list_, 0); --row;
                for (int i = 0; i < row; ++i) SetText(i, 0, std::to_wstring(i + 1));
            }
            rows_.push_back(std::move(record));
            LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = row;
            item.pszText = const_cast<wchar_t*>(L"");
            const int inserted = static_cast<int>(SendMessageW(list_, LVM_INSERTITEMW, 0,
                reinterpret_cast<LPARAM>(&item)));
            if (inserted < 0) { rows_.pop_back(); return; }
        } else *found = std::move(record);
        const auto& data = rows_[static_cast<size_t>(row)];
        const std::wstring values[] = {std::to_wstring(row + 1), data.callback, data.payload, data.response, data.state, data.channel, data.elapsed};
        for (int col = 0; col < 7; ++col) SetText(row, col, values[col]);
        latestRow_ = row;
        const int selected = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (output_ && (selected < 0 || selected == row)) SetWindowTextW(output_, Details(data).c_str());
        InvalidateRect(list_, nullptr, FALSE);
    }

    void Shutdown() {
        if (IsWindow(detail_)) DestroyWindow(detail_);
        detail_ = nullptr;
        if (IsWindow(tab_) && tabIndex_ >= 0 && TabCtrl_GetCurSel(tab_) == tabIndex_) {
            TabCtrl_SetCurSel(tab_, std::max(0, lastNativeTab_));
            NMHDR notify{tab_, static_cast<UINT_PTR>(GetDlgCtrlID(tab_)), TCN_SELCHANGE};
            SendMessageW(tabParent_, WM_NOTIFY, notify.idFrom, reinterpret_cast<LPARAM>(&notify));
        }
        if (tab_) RemoveWindowSubclass(tab_, TabProc, tabSubclassId_);
        if (tabParent_) RemoveWindowSubclass(tabParent_, ParentProc, subclassId_);
        DestroyPage();
        rows_.clear();
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
    int lastNativeTab_ = 0;
    bool registered_ = false;
    std::vector<Record> rows_;
    HWND detail_ = nullptr;
    int latestRow_ = -1;
    JumpCallback jump_;

    static bool IsClass(HWND window, const wchar_t* expected) {
        wchar_t name[128]{}; GetClassNameW(window, name, 128);
        return _wcsicmp(name, expected) == 0;
    }
    struct Search { HWND page = nullptr, tab = nullptr; };
    static BOOL CALLBACK FindTabProc(HWND window, LPARAM value) {
        auto& search = *reinterpret_cast<Search*>(value);
        if (!IsClass(window, WC_TABCONTROLW) || !IsWindowVisible(window)) return TRUE;
        for (int i = 0; i < TabCtrl_GetItemCount(window); ++i) {
            wchar_t text[64]{}; TCITEMW item{}; item.mask = TCIF_TEXT;
            item.pszText = text; item.cchTextMax = 64;
            SendMessageW(window, TCM_GETITEMW, i, reinterpret_cast<LPARAM>(&item));
            if (wcscmp(text, L"属性") == 0) {
                search.tab = window; search.page = GetParent(window); return FALSE;
            }
        }
        return TRUE;
    }
    void FindHost() {
        Search search{};
        EnumChildWindows(main_, FindTabProc, reinterpret_cast<LPARAM>(&search));
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
                if (header->code == NM_CLICK || header->code == NM_DBLCLK)
                    self->Click(*reinterpret_cast<const NMITEMACTIVATE*>(lp), header->code == NM_DBLCLK);
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
        if (message == WM_NOTIFY) {
            const auto* header = reinterpret_cast<const NMHDR*>(lp);
            if (header && header->hwndFrom == self.tab_ && header->code == TCN_SELCHANGE &&
                TabCtrl_GetCurSel(self.tab_) == self.tabIndex_) {
                // Do not send an extra, plugin-owned tab index to the IDE.
                self.UpdateVisibility(); return 0;
            }
        }
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
        if (message == WM_LBUTTONUP || message == WM_KEYUP || message == WM_MBUTTONUP || message == TCM_SETCURSEL)
            self.UpdateVisibility();
        return result;
    }
    void UpdateVisibility() {
        if (!tab_ || !page_) return;
        const int selected = static_cast<int>(SendMessageW(tab_, TCM_GETCURSEL, 0, 0));
        if (selected == tabIndex_)
            SetWindowPos(page_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        else {
            if (selected >= 0) lastNativeTab_ = selected;
            ShowWindow(page_, SW_HIDE);
        }
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
    static std::wstring Details(const Record& row) {
        return L"回调：" + row.callback + L"\r\n频道：" + row.channel + L"\r\n状态：" + row.state +
            L"\r\n耗时：" + row.elapsed + L"\r\n\r\n参数 JSON：\r\n" + row.payload +
            L"\r\n\r\n返回 / 错误：\r\n" + row.response;
    }
    void Click(const NMITEMACTIVATE& click, bool twice) {
        if (click.iItem < 0 || static_cast<size_t>(click.iItem) >= rows_.size()) return;
        const auto row = rows_[static_cast<size_t>(click.iItem)];
        SetWindowTextW(output_, Details(row).c_str());
        if (twice && click.iSubItem == 1 && jump_ && !row.callback.empty()) {
            jump_(row.assembly, row.callback);
            if (IsReady()) { TabCtrl_SetCurSel(tab_, tabIndex_); UpdateVisibility(); }
            return;
        }
        if (twice) { ShowDetails(row); return; }
        const std::wstring value = click.iSubItem == 2 ? row.payload : click.iSubItem == 3 ? row.response :
            click.iSubItem == 5 ? row.channel : Details(row);
        if (Copy(value)) SetWindowTextW(title_, L"通信诊断 - 已复制");
    }
    bool Copy(const std::wstring& value) {
        const size_t bytes = (value.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!memory) return false;
        void* data = GlobalLock(memory);
        if (!data) { GlobalFree(memory); return false; }
        memcpy(data, value.c_str(), bytes); GlobalUnlock(memory);
        if (!OpenClipboard(page_)) { GlobalFree(memory); return false; }
        const bool ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
        CloseClipboard(); if (!ok) GlobalFree(memory); return ok;
    }
    void ShowDetails(const Record& row) {
        if (!IsWindow(detail_)) {
            WNDCLASSW wc{}; wc.hInstance = module_; wc.lpfnWndProc = DetailProc;
            wc.lpszClassName = L"JadeDesigner.DiagnosticDetail";
            wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            RegisterClassW(&wc);
            RECT parent{}; GetWindowRect(main_, &parent);
            detail_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"通信详情 - 参数与返回 JSON",
                WS_OVERLAPPEDWINDOW, parent.left + 40, parent.top + 40, 720, 520, main_, nullptr, module_, nullptr);
            if (!detail_) return;
            HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                0, 0, 0, 0, detail_, reinterpret_cast<HMENU>(1), module_, nullptr);
            SendMessageW(edit, EM_SETLIMITTEXT, 0, 0);
        }
        SetWindowTextW(GetDlgItem(detail_, 1), Details(row).c_str());
        RECT client{}; GetClientRect(detail_, &client);
        MoveWindow(GetDlgItem(detail_, 1), 0, 0, client.right, client.bottom, TRUE);
        ShowWindow(detail_, SW_SHOWNOACTIVATE);
    }
    static LRESULT CALLBACK DetailProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_SIZE)
            MoveWindow(GetDlgItem(window, 1), 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return DefWindowProcW(window, message, wp, lp);
    }
    void DestroyPage() {
        if (page_ && IsWindow(page_)) DestroyWindow(page_);
        page_ = title_ = list_ = outputLabel_ = output_ = nullptr;
        if (tab_ && tabIndex_ >= 0) SendMessageW(tab_, TCM_DELETEITEM, tabIndex_, 0);
        tabIndex_ = -1;
    }
};
