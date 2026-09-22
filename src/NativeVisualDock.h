#pragma once

#include <Windows.h>
#include <CommCtrl.h>

#include <algorithm>
#include <cwctype>
#include <functional>
#include <string>
#include <vector>
#include <utility>
#include "DesignerControlCatalog.h"
#include "DesignerText.h"

// A WPE-only replacement view for the IDE's existing 属性 tab. It reuses the
// native tab instead of adding another visible tool page.
class NativeVisualDock {
public:
    struct Selection {
        std::wstring type;
        std::wstring id;
        std::wstring text;
        std::wstring handler;
        std::wstring channel;
        std::wstring call;
        std::wstring assembly;
        std::wstring left;
        std::wstring top;
        std::wstring width;
        std::wstring height;
        bool designMode = false;
        unsigned long long documentGeneration = 0;
    };
    using EditCallback = std::function<bool(const Selection&, const std::wstring&, const std::wstring&)>;
    using EventCallback = std::function<std::wstring(
        const Selection&, const std::wstring&, const std::string&)>;

    void Initialize(HWND main, HINSTANCE module, EditCallback edit = {}, EventCallback event = {}) {
        main_ = main; module_ = module; edit_ = std::move(edit); event_ = std::move(event);
    }

    bool IsReady() const {
        return page_ && IsWindow(page_) && tab_ && IsWindow(tab_) && tabIndex_ >= 0;
    }

    void Ensure() {
        if (page_ && IsWindow(page_)) return;
        page_ = title_ = info_ = events_ = nullptr;
        tab_ = tabParent_ = nullptr;
        tabIndex_ = -1;
        if (!IsWindow(main_)) return;
        FindHost();
        if (!tab_ || !tabParent_) return;

        RegisterPageClass();
        page_ = CreateWindowExW(WS_EX_CONTROLPARENT, pageClass_, L"属性",
            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, tabParent_, nullptr,
            module_, this);
        if (!page_) return;
        title_ = CreateWindowExW(0, L"STATIC", L"Jade网页控件",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 8, 4, 220, 28,
            page_, nullptr, module_, nullptr);
        info_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            4, 34, 100, 100, page_, nullptr, module_, nullptr);
        events_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            4, 140, 100, 100, page_, nullptr, module_, nullptr);
        if (!title_ || !info_ || !events_) { DestroyPage(); return; }
        ListView_SetExtendedListViewStyle(info_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        ListView_SetExtendedListViewStyle(events_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        AddColumn(info_, 0, L"属性", 120); AddColumn(info_, 1, L"值", 480);
        AddColumn(events_, 0, L"可视化事件", 220); AddColumn(events_, 1, L"状态", 180);
        SetWindowSubclass(tabParent_, ParentProc, subclassId_, reinterpret_cast<DWORD_PTR>(this));
        SetWindowSubclass(tab_, TabProc, tabSubclassId_, reinterpret_cast<DWORD_PTR>(this));
        Layout(); UpdateVisibility();
        Clear();
    }

    void Layout() {
        if (!page_ || !tabParent_ || !tab_) return;
        RECT content{}; GetClientRect(tab_, &content);
        TabCtrl_AdjustRect(tab_, FALSE, &content);
        MapWindowPoints(tab_, tabParent_, reinterpret_cast<POINT*>(&content), 2);
        const int width = std::max(0L, content.right - content.left);
        const int height = std::max(0L, content.bottom - content.top);
        SetWindowPos(page_, HWND_TOP, content.left, content.top, width, height, SWP_NOACTIVATE);
        MoveWindow(title_, 8, 4, std::max(0, width - 16), 28, TRUE);
        const int infoHeight = std::max(76, (height - 46) * 2 / 3);
        MoveWindow(info_, 4, 34, std::max(0, width - 8), infoHeight, TRUE);
        MoveWindow(events_, 4, 40 + infoHeight, std::max(0, width - 8),
            std::max(50, height - 40 - infoHeight), TRUE);
        if (width > 0) {
            ListView_SetColumnWidth(info_, 1, std::max(160, width - 132));
            ListView_SetColumnWidth(events_, 1, std::max(120, width - 232));
        }
    }

    void Clear() {
        Ensure();
        FinishEditor(false);
        selection_ = {};
        if (!info_ || !events_) return;
        ListView_DeleteAllItems(info_); ListView_DeleteAllItems(events_);
        eventRows_.clear();
        SetWindowTextW(title_, L"Jade网页控件");
        SetValue(L"状态", L"请点击 Jade预览中的网页控件");
        SetValue(L"来源", L"仅在检测到 WPE 页面时启用");
        AddEvent(L"事件", L"选择网页控件后显示模块支持的事件");
    }

    void SetEventsEnabled(bool enabled) {
        eventsEnabled_=enabled;
        if(events_&&IsWindow(events_)&&!selection_.id.empty()) {
            ListView_DeleteAllItems(events_);eventRows_.clear();AddSupportedEvents(selection_.type);
        }
    }

    void SetSelection(const Selection& selection) {
        Ensure();
        if (!info_ || !events_) return;
        FinishEditor(false);
        selection_ = selection;
        ListView_DeleteAllItems(info_); ListView_DeleteAllItems(events_);
        eventRows_.clear();
        SetWindowTextW(title_, L"Jade网页控件");
        SetValue(L"状态", L"已选中网页控件");
        SetValue(L"控件类型", selection.type);
        SetValue(L"控件 ID", selection.id);
        SetValue(L"显示文字", selection.text);
        SetValue(L"左边", selection.left.empty() ? L"未读取" : selection.left);
        SetValue(L"顶边", selection.top.empty() ? L"未读取" : selection.top);
        SetValue(L"宽度", selection.width.empty() ? L"未读取" : selection.width);
        SetValue(L"高度", selection.height.empty() ? L"未读取" : selection.height);
        SetValue(L"回调", selection.handler.empty() ? L"未设置" : selection.handler);
        SetValue(L"频道", selection.channel.empty() ? L"未设置" : selection.channel);
        SetValue(L"调用方式", selection.call.empty() ? L"未设置" : selection.call);
        SetValue(L"程序集", selection.assembly.empty() ? L"默认程序集" : selection.assembly);
        SetValue(L"公开成员", PublicMethods(selection.type));
        AddSupportedEvents(selection.type);
        // A virtualized super-list is a scrollable div rather than a native
        // button. Keep its property page visible even when the page is in
        // event mode; otherwise the selection arrives but remains hidden
        // behind the previously active IDE tab.
        if ((selection_.designMode || selection_.type == L"super-list") && tabIndex_ >= 0) {
            SendMessageW(tab_, TCM_SETCURSEL, tabIndex_, 0);
            UpdateVisibility();
        }
    }

    void Shutdown() {
        FinishEditor(false);
        if (tab_ && IsWindow(tab_)) RemoveWindowSubclass(tab_, TabProc, tabSubclassId_);
        if (tabParent_ && IsWindow(tabParent_)) RemoveWindowSubclass(tabParent_, ParentProc, subclassId_);
        DestroyPage();
        main_ = tab_ = tabParent_ = nullptr; tabIndex_ = -1;
    }

private:
    inline static constexpr wchar_t pageClass_[] = L"JadeDesigner.NativeVisualDock";
    inline static constexpr UINT_PTR subclassId_ = 0x4A440040;
    inline static constexpr UINT_PTR tabSubclassId_ = 0x4A440041;
    HWND main_ = nullptr, tabParent_ = nullptr, tab_ = nullptr;
    HWND page_ = nullptr, title_ = nullptr, info_ = nullptr, events_ = nullptr;
    HWND editor_ = nullptr;
    HINSTANCE module_ = nullptr; int tabIndex_ = -1; bool registered_ = false;
    Selection selection_{};
    EditCallback edit_;
    EventCallback event_;
    bool eventsEnabled_=false,eventBusy_=false;
    struct EventRow {
        std::wstring label;
        std::string code;
    };
    std::vector<EventRow> eventRows_;
    int editingRow_ = -1;
    std::wstring editingKey_;

    static bool IsClass(HWND window, const wchar_t* expected) {
        wchar_t name[128]{}; GetClassNameW(window, name, 128);
        return _wcsicmp(name, expected) == 0;
    }
    static bool HasText(HWND window, const wchar_t* expected) {
        wchar_t text[128]{}; GetWindowTextW(window, text, 128);
        return wcscmp(text, expected) == 0;
    }
    struct Search { HWND page = nullptr, tab = nullptr; int index = -1; };
    static BOOL CALLBACK FindTab(HWND window, LPARAM value) {
        auto& search = *reinterpret_cast<Search*>(value);
        if (search.tab || !IsClass(window, WC_TABCONTROLW) || !IsWindowVisible(window)) return TRUE;
        const int count = static_cast<int>(SendMessageW(window, TCM_GETITEMCOUNT, 0, 0));
        for (int index = 0; index < count; ++index) {
            wchar_t text[64]{};
            TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = text;
            item.cchTextMax = ARRAYSIZE(text);
            if (SendMessageW(window, TCM_GETITEMW, index, reinterpret_cast<LPARAM>(&item)) &&
                wcscmp(text, L"属性") == 0) {
                search.tab = window;
                search.page = GetParent(window);
                search.index = index;
                break;
            }
        }
        return TRUE;
    }
    void FindHost() {
        Search search{};
        EnumChildWindows(main_, FindTab, reinterpret_cast<LPARAM>(&search));
        tabParent_ = search.page; tab_ = search.tab; tabIndex_ = search.index;
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
        auto* self = reinterpret_cast<NativeVisualDock*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_COMMAND && self && reinterpret_cast<HWND>(lp) == self->editor_ &&
            HIWORD(wp) == EN_KILLFOCUS) self->FinishEditor(true);
        if (message == WM_NOTIFY && self) {
            const auto* header = reinterpret_cast<const NMHDR*>(lp);
            if (header && header->hwndFrom == self->info_ && header->code == NM_DBLCLK) {
                const auto* click = reinterpret_cast<const NMITEMACTIVATE*>(lp);
                if (click->iSubItem == 1 && click->iItem >= 0) self->BeginEditor(click->iItem);
            }
            if (header && header->hwndFrom == self->events_ && header->code == NM_DBLCLK) {
                const auto* click = reinterpret_cast<const NMITEMACTIVATE*>(lp);
                if (click->iItem >= 0 && static_cast<size_t>(click->iItem) < self->eventRows_.size()) {
                    const auto row = self->eventRows_[static_cast<size_t>(click->iItem)];
                    if (self->event_ && !row.code.empty() && !self->eventBusy_ && self->eventsEnabled_) {
                        // Navigation may pump messages and replace selection_/eventRows_.
                        const auto selection=self->selection_;
                        self->eventBusy_=true;
                        std::wstring result;
                        try {result = self->event_(selection, row.label, row.code);}
                        catch (...) {result=L"事件操作未完成，请检查工程状态";}
                        self->eventBusy_=false;
                        if (!result.empty()) {
                            SetWindowTextW(self->title_, (L"Jade网页控件 - " + result).c_str());
                            if(self->IsReady()&&selection.id==self->selection_.id &&
                                selection.documentGeneration==self->selection_.documentGeneration) {
                                SendMessageW(self->tab_,TCM_SETCURSEL,self->tabIndex_,0);
                                self->UpdateVisibility();
                            }
                        }
                    }
                }
            }
        }
        return DefWindowProcW(window, message, wp, lp);
    }
    static LRESULT CALLBACK EditProc(HWND window, UINT message, WPARAM wp, LPARAM lp,
        UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<NativeVisualDock*>(data);
        if (message == WM_KEYDOWN && wp == VK_RETURN) { self.FinishEditor(true); return 0; }
        if (message == WM_KEYDOWN && wp == VK_ESCAPE) { self.FinishEditor(false); return 0; }
        return DefSubclassProc(window, message, wp, lp);
    }
    static LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<NativeVisualDock*>(data); const LRESULT result = DefSubclassProc(window, message, wp, lp);
        if (message == WM_SIZE || message == WM_WINDOWPOSCHANGED) self.Layout();
        if (message == WM_NOTIFY) { auto* header = reinterpret_cast<const NMHDR*>(lp);
            if (header && header->hwndFrom == self.tab_ && header->code == TCN_SELCHANGE) self.UpdateVisibility(); }
        return result;
    }
    static LRESULT CALLBACK TabProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
        auto& self = *reinterpret_cast<NativeVisualDock*>(data); const LRESULT result = DefSubclassProc(window, message, wp, lp);
        if (message == WM_LBUTTONUP || message == WM_KEYUP || message == WM_MBUTTONUP) self.UpdateVisibility();
        return result;
    }
    static const wchar_t* EditableKey(int row) {
        switch (row) {
        case 3: return L"text";
        case 4: return L"left";
        case 5: return L"top";
        case 6: return L"width";
        case 7: return L"height";
        default: return nullptr;
        }
    }
    void BeginEditor(int row) {
        const wchar_t* key = EditableKey(row);
        if (!key || !info_ || !page_ || editor_) return;
        RECT rect{}; rect.left = 1; rect.top = LVIR_BOUNDS;
        if (!SendMessageW(info_, LVM_GETSUBITEMRECT, row,
            reinterpret_cast<LPARAM>(&rect))) return;
        POINT points[2]{{rect.left, rect.top}, {rect.right, rect.bottom}};
        MapWindowPoints(info_, page_, points, 2);
        wchar_t value[4096]{}; LVITEMW item{}; item.iSubItem = 1;
        item.pszText = value; item.cchTextMax = ARRAYSIZE(value);
        SendMessageW(info_, LVM_GETITEMTEXTW, row, reinterpret_cast<LPARAM>(&item));
        editor_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            points[0].x, points[0].y, std::max<int>(40, points[1].x - points[0].x),
            std::max<int>(20, points[1].y - points[0].y), page_, nullptr, module_, this);
        if (!editor_) return;
        editingRow_ = row; editingKey_ = key;
        SetWindowSubclass(editor_, EditProc, editSubclassId_, reinterpret_cast<DWORD_PTR>(this));
        SendMessageW(editor_, EM_SETSEL, 0, -1); SetFocus(editor_);
    }
    void FinishEditor(bool commit) {
        if (!editor_) return;
        HWND editor = editor_;
        std::wstring value;
        if (commit) {
            wchar_t buffer[4096]{}; GetWindowTextW(editor, buffer, ARRAYSIZE(buffer)); value = buffer;
        }
        if (commit && edit_ && edit_(selection_, editingKey_, value)) {
            if (editingKey_ == L"text") selection_.text = value;
            else {
                auto display = value;
                while (!display.empty() && iswspace(display.back())) display.pop_back();
                if (display.size() >= 2 && display.ends_with(L"px")) {
                    display.resize(display.size() - 2);
                    while (!display.empty() && iswspace(display.back())) display.pop_back();
                }
                if (editingKey_ == L"left") selection_.left = display;
                else if (editingKey_ == L"top") selection_.top = display;
                else if (editingKey_ == L"width") selection_.width = display;
                else if (editingKey_ == L"height") selection_.height = display;
                SetValueAt(editingRow_, display);
            }
            /* SetValueAt keeps the property table stable while the editor closes. */
        }
        RemoveWindowSubclass(editor, EditProc, editSubclassId_);
        editor_ = nullptr; editingRow_ = -1; editingKey_.clear(); DestroyWindow(editor);
    }
    void UpdateVisibility() {
        if (!tab_ || !page_) return;
        const int selected = static_cast<int>(SendMessageW(tab_, TCM_GETCURSEL, 0, 0));
        ShowWindow(page_, selected == tabIndex_ ? SW_SHOW : SW_HIDE);
    }
    void AddColumn(HWND list, int index, const wchar_t* text, int width) {
        LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.pszText = const_cast<wchar_t*>(text); column.cx = width;
        SendMessageW(list, LVM_INSERTCOLUMNW, index, reinterpret_cast<LPARAM>(&column));
    }
    void SetValue(const std::wstring& key, const std::wstring& value) {
        const int row = ListView_GetItemCount(info_); LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = row;
        item.pszText = const_cast<wchar_t*>(key.c_str()); SendMessageW(info_, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
        LVITEMW text{}; text.iSubItem = 1; text.pszText = const_cast<wchar_t*>(value.c_str()); SendMessageW(info_, LVM_SETITEMTEXTW, row, reinterpret_cast<LPARAM>(&text));
    }
    void SetValueAt(int row, const std::wstring& value) {
        if (!info_ || row < 0) return;
        LVITEMW text{}; text.iSubItem = 1; text.pszText = const_cast<wchar_t*>(value.c_str());
        SendMessageW(info_, LVM_SETITEMTEXTW, row, reinterpret_cast<LPARAM>(&text));
    }
    void AddEvent(const std::wstring& name, const std::wstring& state, std::string code = {}) {
        const int row = ListView_GetItemCount(events_); LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = row;
        EventRow eventRow{name, std::move(code)};
        item.mask |= LVIF_PARAM; item.lParam = static_cast<LPARAM>(eventRows_.size());
        item.pszText = const_cast<wchar_t*>(name.c_str()); SendMessageW(events_, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
        LVITEMW text{}; text.iSubItem = 1; text.pszText = const_cast<wchar_t*>(state.c_str()); SendMessageW(events_, LVM_SETITEMTEXTW, row, reinterpret_cast<LPARAM>(&text));
        eventRows_.push_back(std::move(eventRow));
    }
    static std::wstring PublicMethods(const std::wstring& type) {
        const auto kind=type==L"list"?"super-list":DesignerText::Utf8(type);
        for(const auto& entry:DesignerControlCatalog::PublicMethods)
            if(entry.type==kind&&!entry.names.empty())return DesignerText::Wide(std::string(entry.names));
        return L"由对应控件对象提供";
    }
    void AddSupportedEvents(const std::wstring& type) {
        const auto kind=type==L"list"?"super-list":DesignerText::Utf8(type);
        for(const auto& event:DesignerControlCatalog::Events)if(event.type==kind)
            AddEvent(DesignerText::Wide(std::string(event.label)),eventsEnabled_?
                L"双击可自动创建或跳转":L"预览模式只读",std::string(event.code));
        if(eventRows_.empty())AddEvent(L"事件",L"当前控件没有已公开的可视化事件");
    }
    void DestroyPage() {
        FinishEditor(false);
        if (page_ && IsWindow(page_)) DestroyWindow(page_);
        page_ = title_ = info_ = events_ = nullptr;
        tabIndex_ = -1;
    }

    inline static constexpr UINT_PTR editSubclassId_ = 0x4A440042;
};
