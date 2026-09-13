#pragma once

#include <Windows.h>
#include <CommCtrl.h>
#include <algorithm>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

// Own palette windows only. Never replace the IDE's component tree or commands.
class NativeToolbox {
public:
    struct Item { const wchar_t* kind; const wchar_t* title; };
    inline static constexpr Item items[] = {
        {L"pointer", L"指针"}, {L"button", L"按钮"}, {L"label", L"标签"},
        {L"heading", L"标题"}, {L"input", L"输入框"}, {L"textarea", L"多行输入"},
        {L"checkbox", L"复选框"}, {L"radio", L"单选框"}, {L"select", L"下拉框"},
        {L"container", L"布局容器"}, {L"progress", L"进度条"}
    };
    void Initialize(HWND main, HINSTANCE module, std::function<void()> toggle,
        std::function<void(const std::wstring&)> selected) {
        main_ = main; module_ = module; toggle_ = std::move(toggle); selected_ = std::move(selected);
    }
    bool Configure(bool enabled, bool active, std::wstring& error) {
        enabled_ = enabled;
        if (!enabled || !active) { Leave(); return true; }
        const bool ready = Enter(error);
        if (!ready) enabled_ = false;
        return ready;
    }
    void Update(bool active) {
        if (busy_) return;
        if (!enabled_ || !active) { Leave(); return; }
        if (panel_ && !floating_ && (!IsWindow(dialog_) || !IsWindowVisible(dialog_))) {
            enabled_ = false; Leave();
            if (selected_) selected_(L"unavailable");
            return;
        }
        std::wstring error;
        if (!Enter(error)) { enabled_ = false; if (selected_) selected_(L"unavailable"); }
        Layout();
    }
    void Pointer() { if (list_) SendMessageW(list_, LB_SETCURSEL, 0, 0); }
    bool IsFloating() const { return floating_ && panel_; }
    void SetEditable(bool editable) {
        const bool changed = editable_ != editable; editable_ = editable;
        if (list_) EnableWindow(list_, editable ? TRUE : FALSE);
        if (!editable && changed) { Pointer(); if (selected_) selected_(L"pointer"); }
    }
    void Shutdown() {
        enabled_ = false; Leave();
        main_ = nullptr; toggle_ = {}; selected_ = {};
        if (registered_) { UnregisterClassW(kClass, module_); registered_ = false; }
    }
    ~NativeToolbox() { Shutdown(); }

private:
    inline static constexpr wchar_t kClass[] = L"JadeDesigner.NativeToolbox";
    inline static constexpr UINT_PTR kSubclass = 0x4A440020;
    HWND main_ = nullptr, dialog_ = nullptr, panel_ = nullptr, list_ = nullptr, title_ = nullptr;
    HINSTANCE module_ = nullptr;
    HFONT font_ = nullptr;
    bool enabled_ = false, opened_ = false, busy_ = false, registered_ = false, editable_ = true, floating_ = false;
    RECT floatingRect_{};
    std::vector<HWND> hidden_;
    std::function<void()> toggle_;
    std::function<void(const std::wstring&)> selected_;
    struct Guard { bool& value; explicit Guard(bool& v) : value(v) { value = true; } ~Guard() { value = false; } };

    static bool ClassIs(HWND window, const wchar_t* expected) {
        wchar_t name[128]{}; GetClassNameW(window, name, 128);
        return _wcsicmp(name, expected) == 0;
    }
    static bool TextIs(HWND window, const wchar_t* expected) {
        wchar_t text[128]{}; GetWindowTextW(window, text, 128);
        return wcscmp(text, expected) == 0;
    }
    static int Dpi(HWND window) {
        using Query = UINT(WINAPI*)(HWND);
        static const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (query) { const auto dpi = query(window); if (dpi) return static_cast<int>(dpi); }
        HDC dc = GetDC(window); const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
        if (dc) ReleaseDC(window, dc);
        return dpi > 0 ? dpi : 96;
    }
    HWND FindDialog() const {
        struct Search { HWND main, found = nullptr; unsigned count = 0; } search{main_};
        EnumChildWindows(main_, [](HWND window, LPARAM data) -> BOOL {
            auto& s = *reinterpret_cast<Search*>(data);
            if (!ClassIs(window, L"#32770")) return TRUE;
            const HWND tree = GetDlgItem(window, 1225), basic = GetDlgItem(window, 11225), extra = GetDlgItem(window, 11226);
            if (!ClassIs(tree, WC_TREEVIEWW) || GetParent(tree) != window ||
                !ClassIs(basic, L"Button") || !TextIs(basic, L"基本组件") ||
                !ClassIs(extra, L"Button") || !TextIs(extra, L"扩展组件")) return TRUE;
            const HWND owner = GetParent(window);
            if (!owner || GetDlgCtrlID(owner) != 110 || !TextIs(owner, L"窗口组件箱")) return TRUE;
            DWORD process = 0; GetWindowThreadProcessId(window, &process);
            if (process != GetCurrentProcessId() || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) return TRUE;
            ++s.count; s.found = window; return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.count == 1 ? search.found : nullptr;
    }
    void Layout() {
        if (!panel_) return;
        RECT r{}; GetClientRect(floating_ ? panel_ : dialog_, &r);
        const int width = std::max(0L, r.right), height = std::max(0L, r.bottom);
        if (!floating_) SetWindowPos(panel_, HWND_TOP, 0, 0, width, height, SWP_NOACTIVATE);
        const int heading = MulDiv(30, Dpi(panel_), 96);
        MoveWindow(title_, 6, 0, std::max(0, width - 12), heading, TRUE);
        MoveWindow(list_, 4, heading, std::max(0, width - 8), std::max(0, height - heading - 4), TRUE);
    }
    static LRESULT CALLBACK DialogProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<NativeToolbox*>(data);
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, DialogProc, kSubclass);
            self.dialog_ = nullptr; self.opened_ = false; self.hidden_.clear();
        }
        const auto result = DefSubclassProc(window, message, wp, lp);
        if (message == WM_SIZE && !self.busy_) self.Layout();
        return result;
    }
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<NativeToolbox*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<NativeToolbox*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && self->floating_ && message == WM_CLOSE) {
            self->enabled_ = false; self->Leave();
            if (self->selected_) self->selected_(L"unavailable");
            return 0;
        }
        if (self && self->floating_ && message == WM_SIZE && !self->busy_) self->Layout();
        if (self && self->floating_ && message == WM_GETMINMAXINFO) {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lp);
            limits->ptMinTrackSize = {MulDiv(160,Dpi(window),96),MulDiv(220,Dpi(window),96)};
            return 0;
        }
        if (self && message == WM_COMMAND && reinterpret_cast<HWND>(lp) == self->list_ && HIWORD(wp) == LBN_SELCHANGE) {
            const LRESULT index = SendMessageW(self->list_, LB_GETCURSEL, 0, 0);
            if (self->editable_ && index >= 0 && index < static_cast<LRESULT>(std::size(items)) && self->selected_) self->selected_(items[index].kind);
            return 0;
        }
        if (self && message == WM_NCDESTROY) {
            self->panel_ = self->list_ = self->title_ = nullptr;
            if (self->font_) { DeleteObject(self->font_); self->font_ = nullptr; }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(window, message, wp, lp);
    }
    bool Enter(std::wstring& error) {
        if (panel_) return true;
        if (busy_) { error = L"组件箱正在切换，请稍后重试"; return false; }
        Guard guard(busy_);
        if (!IsWindow(main_)) { error=L"易语言主窗口不可用"; return false; }
        dialog_ = FindDialog();
        if (dialog_ && !IsWindowVisible(dialog_) && toggle_) {
            toggle_(); opened_ = IsWindowVisible(dialog_) != FALSE;
        }
        RECT r{}; GetClientRect(dialog_, &r);
        if (!dialog_ || !IsWindowVisible(dialog_) || r.right < 30 || r.bottom < 60) {
            // A windowless project may never expose the IDE's native palette.
            // Own a tool window instead; do not create a project window or force
            // visibility on an unrecognized IDE pane.
            Rollback(); floating_ = true;
        }
        if (!registered_) {
            WNDCLASSW wc{}; wc.hInstance = module_; wc.lpfnWndProc = WindowProc; wc.lpszClassName = kClass;
            wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            if (!RegisterClassW(&wc)) { error = L"无法注册网页组件箱"; Rollback(); return false; }
            registered_ = true;
        }
        if (!floating_ && !SetWindowSubclass(dialog_, DialogProc, kSubclass, reinterpret_cast<DWORD_PTR>(this))) {
            error = L"无法跟随原生组件箱布局"; Rollback(); return false;
        }
        if (floating_) {
            RECT owner{}; GetWindowRect(main_,&owner);
            MONITORINFO monitor{sizeof(monitor)};
            if (!GetMonitorInfoW(MonitorFromWindow(main_,MONITOR_DEFAULTTONEAREST),&monitor)) { error=L"无法定位网页组件箱"; Rollback(); return false; }
            const auto& work=monitor.rcWork;
            const int width=std::min(MulDiv(220,Dpi(main_),96),static_cast<int>(work.right-work.left));
            const int height=std::min(MulDiv(440,Dpi(main_),96),static_cast<int>(work.bottom-work.top));
            const int x=std::clamp(floatingRect_.right?floatingRect_.left:owner.right-width-16,work.left,work.right-width);
            const int y=std::clamp(floatingRect_.bottom?floatingRect_.top:owner.top+90,work.top,work.bottom-height);
            panel_ = CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,kClass,L"Jade 网页组件（独立）",
                WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_CLIPCHILDREN,
                x,y,width,height,main_,nullptr,module_,this);
        } else {
            panel_ = CreateWindowExW(WS_EX_CONTROLPARENT, kClass, L"Jade 网页组件", WS_CHILD | WS_CLIPCHILDREN,
                0, 0, r.right, r.bottom, dialog_, nullptr, module_, this);
        }
        if (panel_) {
            title_ = CreateWindowExW(0, L"STATIC", L"Jade 网页组件", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0,0,0,0,panel_,nullptr,module_,nullptr);
            list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"网页控件", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                0,0,0,0,panel_,reinterpret_cast<HMENU>(1),module_,nullptr);
        }
        if (!panel_ || !title_ || !list_) { error = L"创建网页组件列表失败"; Rollback(); return false; }
        const int fontHeight = -MulDiv(14, Dpi(panel_), 96);
        font_ = CreateFontW(fontHeight,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        for (const auto& item : items) SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.title));
        Pointer();
        EnableWindow(list_, editable_ ? TRUE : FALSE);
        for (HWND child = floating_ ? nullptr : GetWindow(dialog_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (child != panel_ && (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) {
                hidden_.push_back(child); ShowWindow(child, SW_HIDE);
            }
        }
        Layout(); ShowWindow(panel_, SW_SHOWNOACTIVATE);
        return true;
    }
    void Rollback() {
        if (floating_ && panel_ && IsWindow(panel_)) GetWindowRect(panel_, &floatingRect_);
        if (dialog_ && IsWindow(dialog_)) RemoveWindowSubclass(dialog_, DialogProc, kSubclass);
        if (panel_ && IsWindow(panel_)) DestroyWindow(panel_);
        for (HWND child : hidden_) if (IsWindow(child) && GetParent(child) == dialog_) ShowWindow(child, SW_SHOWNOACTIVATE);
        hidden_.clear();
        if (opened_ && dialog_ && IsWindowVisible(dialog_) && toggle_) toggle_();
        opened_ = false; floating_ = false; dialog_ = nullptr;
    }
    void Leave() {
        if (busy_) return;
        Guard guard(busy_); const bool wasAttached = panel_ != nullptr; Rollback();
        if (wasAttached && selected_) selected_(L"pointer");
    }
};
