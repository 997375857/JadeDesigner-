#include "WebPreview.h"

#include "DesignerLog.h"
#include "IdeEventRouter.h"
#include "DesignerText.h"
#include "DesignerVisual.h"
#include "NativeToolbox.h"
#include "HookBridge.h"
#include "DesignerInspection.h"
#include "DesignerToolsScript.h"
#include "RuntimeDiagnosticsScript.h"
#include "DiagnosticsInstall.h"

#include <CommCtrl.h>
#include <Shlwapi.h>
#include <WebView2.h>
#include <windowsx.h>
#include <wrl.h>

#include <cwchar>
#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr char kHostClassName[] = "JadeDesigner.PreviewMdiChild";
constexpr wchar_t kCompatTabClassName[] = L"JadeDesigner.CompatCodeTab";
constexpr wchar_t kTabText[] = L"Jade预览";
constexpr UINT_PTR kRefreshTimerId = 0x4A44;
constexpr UINT kRefreshIntervalMs = 600;
constexpr UINT kMaximizeDocumentMessage = WM_APP + 0x4A4;
// WebView2 invokes WebMessageReceived while its controller is still inside
// the browser callback. Editing/activating an e5.95 MDI page from that callback
// can re-enter the preview and crash the IDE. Queue the event to the native
// preview window and handle it after the WebView callback returns.
constexpr UINT kRouteUiEventMessage = WM_APP + 0x4A5;
constexpr UINT kNativeToolboxMessage = WM_APP + 0x4A6;

struct PreviewState {
    HMODULE module = nullptr;
    HWND mainWindow = nullptr;
    HWND mdiClient = nullptr;
    HWND codeTab = nullptr;
    HWND hostWindow = nullptr;
    HWND compatTabWindow = nullptr;
    HWND tabMessageTarget = nullptr;
    HFONT compatTabFont = nullptr;
    bool classesReady = false;
    bool active = false;
    bool webViewStarting = false;
    bool comAttempted = false;
    bool comNeedsUninitialize = false;
    bool currentPageIsFile = false;
    bool lastWriteValid = false;
    bool maximizingDocument = false;
    bool compatTabHover = false;
    bool tabCaptionEverUpdated = false;
    int codeTabIndex = -1;
    unsigned int tabCaptionUpdateAttempts = 0;
    std::wstring indexPath;
    FILETIME lastWrite{};
    bool fitPending = false;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webView;
    EventRegistrationToken navigationToken{};
    bool navigationTokenValid = false;
    EventRegistrationToken navigationStartingToken{};
    bool navigationStartingTokenValid = false;
    unsigned long long documentGeneration = 0;
    EventRegistrationToken webMessageToken{};
    bool webMessageTokenValid = false;
    unsigned long long generation = 0;
    bool shuttingDown = false;
};

PreviewState g_state;
NativeToolbox g_nativeToolbox;
bool g_visualDesignMode = false;

struct PendingUiEvent {
    unsigned long long generation = 0;
    unsigned long long documentGeneration = 0;
    IdeEventRouter::UiEvent event;
    bool commonCommand = false;
    CommonCode::Options commonOptions;
    std::wstring toolMessage, projectPath, indexPath, traceId;
};

DesignerText::Document g_textDocument;
IdeEventRouter::CommonPreview g_commonPreview;
unsigned g_commonPreviewRevision = 0;
std::wstring g_commonPreviewProject;

int MeasureTextWidth(HDC dc, const wchar_t* text, int length)
{
    SIZE size{};
    return GetTextExtentPoint32W(dc, text, length, &size) ? size.cx : 0;
}

HFONT EnsureCompatTabFont()
{
    if (g_state.compatTabFont == nullptr) {
        g_state.compatTabFont = CreateFontW(
            -15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
            L"Microsoft YaHei UI");
    }
    return g_state.compatTabFont != nullptr
        ? g_state.compatTabFont
        : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

void PaintCompatTab(HWND window)
{
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT rect{};
    GetClientRect(window, &rect);
    const bool active = WebPreview::IsActive();
    const COLORREF background = active
        ? RGB(255, 244, 244)
        : (g_state.compatTabHover ? RGB(255, 232, 232) : RGB(240, 240, 240));
    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(dc, &rect, backgroundBrush);
    DeleteObject(backgroundBrush);

    HPEN separatorPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
    HPEN previousPen = static_cast<HPEN>(SelectObject(dc, separatorPen));
    MoveToEx(dc, 0, 3, nullptr);
    LineTo(dc, 0, rect.bottom - 3);
    SelectObject(dc, previousPen);
    DeleteObject(separatorPen);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, active ? RGB(238, 0, 0) : RGB(205, 0, 0));
    HFONT font = EnsureCompatTabFont();
    HFONT previousFont = static_cast<HFONT>(SelectObject(dc, font));
    DrawTextW(dc, kTabText, -1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previousFont);

    if (active) {
        RECT underline = rect;
        underline.top = underline.bottom - 3;
        HBRUSH underlineBrush = CreateSolidBrush(RGB(230, 0, 0));
        FillRect(dc, &underline, underlineBrush);
        DeleteObject(underlineBrush);
    }
    EndPaint(window, &paint);
}

LRESULT CALLBACK CompatTabWindowProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintCompatTab(window);
        return 0;
    case WM_MOUSEMOVE:
        if (!g_state.compatTabHover) {
            g_state.compatTabHover = true;
            TRACKMOUSEEVENT tracking{};
            tracking.cbSize = sizeof(tracking);
            tracking.dwFlags = TME_LEAVE;
            tracking.hwndTrack = window;
            TrackMouseEvent(&tracking);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        g_state.compatTabHover = false;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(window);
        return 0;
    case WM_LBUTTONUP: {
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        RECT rect{};
        GetClientRect(window, &rect);
        if (PtInRect(&rect, point)) {
            WebPreview::Show();
        }
        return 0;
    }
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
        return TRUE;
    case WM_NCDESTROY:
        if (g_state.compatTabWindow == window) {
            g_state.compatTabWindow = nullptr;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int CalculateCompatTabLeft(HDC dc, HFONT font)
{
    int left = 6;
    HFONT previousFont = static_cast<HFONT>(SelectObject(dc, font));
    HWND child = GetWindow(g_state.mdiClient, GW_CHILD);
    while (child != nullptr) {
        if (child != g_state.hostWindow && GetParent(child) == g_state.mdiClient &&
            (GetWindowLongPtrW(child, GWL_EXSTYLE) & WS_EX_MDICHILD) != 0) {
            const int length = GetWindowTextLengthW(child);
            if (length > 0 && length < 2048) {
                std::vector<wchar_t> title(static_cast<size_t>(length) + 1, L'\0');
                const int copied = GetWindowTextW(child, title.data(), length + 1);
                if (copied > 0 && wcscmp(title.data(), kTabText) != 0) {
                    left += MeasureTextWidth(dc, title.data(), copied) + 18;
                }
            }
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
    SelectObject(dc, previousFont);
    return left;
}

void UpdateCompatTabLayout()
{
    if (g_state.compatTabWindow == nullptr || !IsWindow(g_state.compatTabWindow) ||
        g_state.codeTab == nullptr || !IsWindow(g_state.codeTab)) {
        return;
    }
    if (g_state.tabCaptionEverUpdated) {
        ShowWindow(g_state.compatTabWindow, SW_HIDE);
        return;
    }

    RECT client{};
    GetClientRect(g_state.codeTab, &client);
    if (client.right <= 0 || client.bottom <= 0) {
        return;
    }
    HDC dc = GetDC(g_state.codeTab);
    HFONT codeTabFont = reinterpret_cast<HFONT>(
        SendMessageW(g_state.codeTab, WM_GETFONT, 0, 0));
    if (codeTabFont == nullptr) {
        codeTabFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }
    HFONT previousFont = static_cast<HFONT>(SelectObject(dc, EnsureCompatTabFont()));
    const int textWidth = MeasureTextWidth(
        dc, kTabText, static_cast<int>(std::size(kTabText) - 1));
    SelectObject(dc, previousFont);
    int left = CalculateCompatTabLeft(dc, codeTabFont);
    ReleaseDC(g_state.codeTab, dc);

    const int width = textWidth + 30;
    const int maximumLeft = client.right - width - 4;
    if (left > maximumLeft) {
        left = maximumLeft;
    }
    if (left < 2) {
        left = 2;
    }
    SetWindowPos(
        g_state.compatTabWindow, HWND_TOP, left, 0, width, client.bottom,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_state.compatTabWindow, nullptr, FALSE);
}

bool EnsureCompatTabWindow()
{
    if (g_state.compatTabWindow != nullptr && IsWindow(g_state.compatTabWindow)) {
        UpdateCompatTabLayout();
        return true;
    }
    g_state.compatTabWindow = CreateWindowExW(
        0, kCompatTabClassName, kTabText,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 90, 18, g_state.codeTab, nullptr, g_state.module, nullptr);
    if (g_state.compatTabWindow == nullptr) {
        DesignerLog::Write(
            "PREVIEW compatibility tab create failed error=" +
            std::to_string(GetLastError()));
        return false;
    }
    DesignerLog::Write(
        "PREVIEW compatibility tab created hwnd=" +
        DesignerLog::HexPointer(g_state.compatTabWindow));
    UpdateCompatTabLayout();
    return true;
}

int QueryCodeTabCount(HWND messageTarget)
{
    if (messageTarget == nullptr || !IsWindow(messageTarget)) {
        return -1;
    }
    const LRESULT countResult = SendMessageW(
        messageTarget, TCM_GETITEMCOUNT, 0, 0);
    if (countResult < 0 || countResult > 4096) {
        return -1;
    }
    return static_cast<int>(countResult);
}

void CaptureCodeTabInsertionPoint()
{
    g_state.tabMessageTarget = nullptr;
    g_state.codeTabIndex = -1;
    const HWND targets[] = {g_state.mainWindow, g_state.codeTab};
    for (const HWND target : targets) {
        const int count = QueryCodeTabCount(target);
        if (count <= 0) {
            continue;
        }
        g_state.tabMessageTarget = target;
        g_state.codeTabIndex = count;
        DesignerLog::Write(
            "PREVIEW code_tab_insertion_point target=" +
            DesignerLog::HexPointer(target) +
            " index=" + std::to_string(count));
        return;
    }
}

bool SetCodeTabCaptionAt(HWND messageTarget, int index, const char* route)
{
    if (g_state.hostWindow == nullptr || !IsWindow(g_state.hostWindow) ||
        index < 0 || QueryCodeTabCount(messageTarget) <= index) {
        return false;
    }

    TCITEMW updated{};
    updated.mask = TCIF_TEXT | TCIF_PARAM;
    updated.pszText = const_cast<LPWSTR>(kTabText);
    updated.lParam = reinterpret_cast<LPARAM>(g_state.hostWindow);
    const bool succeeded = SendMessageW(
        messageTarget, TCM_SETITEMW,
        static_cast<WPARAM>(index),
        reinterpret_cast<LPARAM>(&updated)) != 0;
    if (!g_state.tabCaptionEverUpdated || !succeeded) {
        DesignerLog::Write(
            "PREVIEW native_code_tab_update route=" + std::string(route) +
            " target=" + DesignerLog::HexPointer(messageTarget) +
            " index=" + std::to_string(index) +
            " success=" + std::to_string(succeeded ? 1 : 0));
    }
    return succeeded;
}

bool UpdateCodeTabCaptionOn(HWND messageTarget)
{
    if (messageTarget == nullptr || !IsWindow(messageTarget) ||
        g_state.hostWindow == nullptr) {
        return false;
    }

    const int count = QueryCodeTabCount(messageTarget);
    if (count <= 0) {
        return false;
    }

    for (int index = 0; index < count; ++index) {
        TCITEMW current{};
        current.mask = TCIF_PARAM;
        if (SendMessageW(
                messageTarget, TCM_GETITEMW,
                static_cast<WPARAM>(index),
                reinterpret_cast<LPARAM>(&current)) == 0) {
            continue;
        }
        if (reinterpret_cast<HWND>(current.lParam) != g_state.hostWindow) {
            continue;
        }
        return SetCodeTabCaptionAt(messageTarget, index, "matched_hwnd");
    }
    return false;
}

bool UpdateCodeTabCaption()
{
    // WM_MDICREATE appends the Jade document at the count captured just
    // before creation. Updating that exact slot avoids relying on a themed
    // tab implementation exposing its HWND through TCM_GETITEM.
    if (g_state.tabMessageTarget != nullptr && g_state.codeTabIndex >= 0 &&
        SetCodeTabCaptionAt(
            g_state.tabMessageTarget, g_state.codeTabIndex,
            "captured_index")) {
        g_state.tabCaptionEverUpdated = true;
        return true;
    }

    // The installed visual extension forwards the standard TCM_* messages
    // from the IDE main window to its custom code-tab model. A plain tab
    // implementation may accept the same messages on the tab HWND itself.
    const bool updated = UpdateCodeTabCaptionOn(g_state.mainWindow) ||
                         UpdateCodeTabCaptionOn(g_state.codeTab);
    g_state.tabCaptionEverUpdated = g_state.tabCaptionEverUpdated || updated;
    return updated;
}

void RetryCodeTabCaption()
{
    constexpr unsigned int kMaximumAttempts = 20;
    if (g_state.tabCaptionUpdateAttempts >= kMaximumAttempts) {
        return;
    }
    ++g_state.tabCaptionUpdateAttempts;
    const bool updated = UpdateCodeTabCaption();
    if (!updated && g_state.tabCaptionUpdateAttempts == kMaximumAttempts) {
        DesignerLog::Write(
            "PREVIEW native_code_tab_update exhausted after 20 attempts");
    }
}

void MaximizeMdiDocument(HWND documentWindow)
{
    if (g_state.maximizingDocument || documentWindow == nullptr ||
        !IsWindow(documentWindow) || g_state.mdiClient == nullptr ||
        !IsWindow(g_state.mdiClient) ||
        GetParent(documentWindow) != g_state.mdiClient) {
        return;
    }

    g_state.maximizingDocument = true;
    ShowWindow(documentWindow, SW_SHOW);

    // The current IDE visual layer intentionally swallows WM_MDIMAXIMIZE.
    // Calling the registered system MDIClient class procedure preserves the
    // native maximized-document layout without bypassing tab activation.
    const auto mdiClassProcedure = reinterpret_cast<WNDPROC>(
        GetClassLongPtrA(g_state.mdiClient, GCLP_WNDPROC));
    if (mdiClassProcedure != nullptr) {
        CallWindowProcA(
            mdiClassProcedure, g_state.mdiClient, WM_MDIMAXIMIZE,
            reinterpret_cast<WPARAM>(documentWindow), 0);
    }
    else {
        ShowWindow(documentWindow, SW_MAXIMIZE);
    }

    DesignerLog::Write(
        "PREVIEW maximize_document hwnd=" +
        DesignerLog::HexPointer(documentWindow) +
        " zoomed=" + std::to_string(IsZoomed(documentWindow) ? 1 : 0));
    g_state.maximizingDocument = false;
}

std::string WideToAnsi(const wchar_t* value)
{
    if (value == nullptr || *value == L'\0') {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_ACP, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_ACP, 0, value, -1, result.data(), required, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::string HResultText(HRESULT value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << static_cast<unsigned long>(value);
    return stream.str();
}

std::wstring GetModuleDirectory()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        g_state.module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return L".";
    }
    std::wstring path(buffer.data(), length);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring BuildIndexPath()
{
    return GetModuleDirectory() + L"\\JadeDesigner\\web\\index.html";
}

bool ReadLastWriteTime(const std::wstring& path, FILETIME& result);

std::wstring DirectoryOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

// The IDE exposes no public function for the current project path and its
// menu bar is custom-drawn (GetMenu returns NULL). A saved project's full
// path appears in the main window title:
//   "程序名 - E:\dir\工程.e [无编译条件] - Windows窗口程序 - [页面]"
// so the title is the most reliable non-intrusive source.
bool ExtractProjectPathFromTitle(const std::wstring& title, std::wstring& projectPath)
{
    for (size_t index = 0; index + 3 < title.size(); ++index) {
        if (iswalpha(title[index]) == 0 ||
            title[index + 1] != L':' || title[index + 2] != L'\\') {
            continue;
        }
        for (size_t end = index + 3; end + 1 < title.size(); ++end) {
            if (title[end] == L'.' &&
                towlower(title[end + 1]) == L'e' &&
                (end + 2 == title.size() || title[end + 2] == L' ' ||
                 title[end + 2] == L'[')) {
                projectPath = title.substr(index, end + 2 - index);
                return true;
            }
        }
    }
    return false;
}

bool ReadRecentProjectPath(std::wstring& projectPath)
{
    if (g_state.mainWindow == nullptr || !IsWindow(g_state.mainWindow)) {
        return false;
    }
    wchar_t title[1024]{};
    GetWindowTextW(g_state.mainWindow, title, 1024);
    if (wcsstr(title, L"[起始页]") != nullptr) {
        return false; // no project loaded in this instance
    }
    if (ExtractProjectPathFromTitle(title, projectPath)) {
        // Polled every 600 ms while the preview is open, so this is the earliest
        // and most reliable point at which the log can learn where the source
        // being worked on lives - no click required.
        DesignerLog::UseProjectFile(projectPath);
        return true;
    }
    static bool loggedTitleMiss = false;
    if (!loggedTitleMiss) {
        loggedTitleMiss = true;
        DesignerLog::Write(
            "PREVIEW project_path_missing title=\"" + DesignerLog::ToUtf8(title) +
            "\"（新建未保存的工程没有路径，回退内置预览页）");
    }
    return false;
}

// Resolves <project dir>\web\ : index.html first, otherwise the first .html
// in the directory. html + css + js split into separate files is fine - the
// page loads them through relative paths.
bool ResolveProjectWebIndex(std::wstring& result)
{
    std::wstring projectPath;
    if (!ReadRecentProjectPath(projectPath)) {
        return false;
    }
    const std::wstring webDir = DirectoryOf(projectPath) + L"\\web";

    std::wstring preferred = webDir + L"\\index.html";
    FILETIME unused{};
    if (ReadLastWriteTime(preferred, unused)) {
        result = preferred;
        return true;
    }

    std::wstring fallbackIndex;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((webDir + L"\\*.htm*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                continue;
            }
            const std::wstring name = data.cFileName;
            if (_wcsnicmp(name.c_str(), L"index", 5) == 0) {
                fallbackIndex = name;
                break;
            }
            if (fallbackIndex.empty()) {
                fallbackIndex = name;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    if (!fallbackIndex.empty()) {
        result = webDir + L"\\" + fallbackIndex;
        return true;
    }
    DesignerLog::Write(
        "PREVIEW project_web_missing dir=\"" + DesignerLog::ToUtf8(webDir) + "\"");
    return false;
}

// Picks the newest write time across all web sources in the directory so that
// editing a css/js alone still triggers the hot reload.
bool ReadWebDirLatestWrite(const std::wstring& webDir, FILETIME& result)
{
    FILETIME best{};
    bool valid = false;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((webDir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        const std::wstring name = data.cFileName;
        const size_t dot = name.find_last_of(L'.');
        const std::wstring extension =
            dot == std::wstring::npos ? std::wstring() : name.substr(dot);
        if (_wcsicmp(extension.c_str(), L".html") != 0 &&
            _wcsicmp(extension.c_str(), L".htm") != 0 &&
            _wcsicmp(extension.c_str(), L".css") != 0 &&
            _wcsicmp(extension.c_str(), L".js") != 0 &&
            _wcsicmp(extension.c_str(), L".mjs") != 0 &&
            _wcsicmp(extension.c_str(), L".json") != 0) {
            continue;
        }
        FILETIME write{};
        if (ReadLastWriteTime(webDir + L"\\" + name, write)) {
            if (!valid || CompareFileTime(&write, &best) > 0) {
                best = write;
                valid = true;
            }
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (valid) {
        result = best;
    }
    return valid;
}

// Re-resolves which index page the preview should show. Returns true when the
// resolved path changed (project opened/switched, or its web dir appeared).
bool RefreshResolvedIndex()
{
    std::wstring resolved;
    const std::string source =
        ResolveProjectWebIndex(resolved) ? "project" : "builtin";
    if (source == "builtin") {
        resolved = BuildIndexPath();
    }
    if (_wcsicmp(resolved.c_str(), g_state.indexPath.c_str()) == 0) {
        return false;
    }
    DesignerLog::Write(
        "PREVIEW web_index_resolved source=" + source +
        " path=\"" + DesignerLog::ToUtf8(resolved) + "\"");
    g_state.indexPath = resolved;
    g_state.lastWriteValid = false;
    return true;
}

std::wstring BuildUserDataFolder()
{
    wchar_t tempPath[MAX_PATH]{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(tempPath)), tempPath);
    std::wstring path = length > 0 && length < std::size(tempPath) ? tempPath : L".";
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path.push_back(L'\\');
    }
    path += L"JadeDesigner_WebView2_5_95";
    CreateDirectoryW(path.c_str(), nullptr);
    return path;
}

bool ReadLastWriteTime(const std::wstring& path, FILETIME& result)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }
    result = data.ftLastWriteTime;
    return true;
}

std::wstring PathToUrl(const std::wstring& path)
{
    std::vector<wchar_t> buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    if (SUCCEEDED(UrlCreateFromPathW(path.c_str(), buffer.data(), &length, 0))) {
        return std::wstring(buffer.data(), length);
    }
    return {};
}

void ResizeController()
{
    if (!g_state.controller || g_state.hostWindow == nullptr) {
        return;
    }
    RECT bounds{};
    GetClientRect(g_state.hostWindow, &bounds);
    const bool boundsChanged =
        !g_state.controller ||
        [&]() {
            RECT previous{};
            g_state.controller->get_Bounds(&previous);
            return previous.left != bounds.left || previous.top != bounds.top ||
                previous.right != bounds.right || previous.bottom != bounds.bottom;
        }();
    g_state.controller->put_Bounds(bounds);
    if (boundsChanged) {
        g_state.fitPending = true;
    }
}

// Scales the web content down (never up) with the WebView2 zoom factor so a
// fixed-size design fits the visible area exactly. Pages opt out or choose
// width-only fitting with <meta name="jade-fit" content="none|width|both">.
void ApplyFitFromMetrics(int cssWidth, int cssHeight, const std::wstring& mode)
{
    if (!g_state.controller || cssWidth <= 0 || cssHeight <= 0 || mode == L"none") {
        return;
    }
    const bool fitHeight = mode != L"width";
    RECT bounds{};
    g_state.controller->get_Bounds(&bounds);
    const double physicalWidth = std::max(1.0, static_cast<double>(bounds.right - bounds.left));
    const double physicalHeight =
        std::max(1.0, static_cast<double>(bounds.bottom - bounds.top));
    double current = 0.0;
    g_state.controller->get_ZoomFactor(&current);
    if (current < 0.05) {
        current = 1.0;
    }
    double target = 1.0;
    if (cssWidth * current > physicalWidth + 1.0) {
        target = std::min(target, physicalWidth / cssWidth);
    }
    if (fitHeight && cssHeight * current > physicalHeight + 1.0) {
        target = std::min(target, physicalHeight / cssHeight);
    }
    if (std::fabs(target - current) > 0.02) {
        const HRESULT result = g_state.controller->put_ZoomFactor(target);
        DesignerLog::Write(
            "PREVIEW fit_zoom hr=" + HResultText(result) +
            " factor=" + std::to_string(target).substr(0, 5) +
            " content=" + std::to_string(cssWidth) + "x" + std::to_string(cssHeight) +
            " mode=" + (mode.empty() ? "both" : DesignerLog::ToUtf8(mode)));
    }
}

void RunFitToWindow()
{
    if (!g_state.webView || !g_state.controller) {
        return;
    }
    constexpr const wchar_t* probe =
        LR"JS((() => { const d = document.documentElement; const m = document.querySelector('meta[name="jade-fit"]'); return d.scrollWidth + 'x' + d.scrollHeight + '|' + (m ? (m.content || '') : ''); })())JS";
    auto handler = Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [](HRESULT result, LPCWSTR raw) -> HRESULT {
            if (FAILED(result) || raw == nullptr) {
                return S_OK;
            }
            const std::wstring json = raw;
            const size_t firstQuote = json.find(L'"');
            const size_t lastQuote = json.rfind(L'"');
            if (firstQuote == std::wstring::npos || lastQuote <= firstQuote) {
                return S_OK;
            }
            const std::wstring payload = json.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            const size_t x = payload.find(L'x');
            const size_t bar = payload.find(L'|');
            if (x == std::wstring::npos || bar == std::wstring::npos || x > bar) {
                return S_OK;
            }
            ApplyFitFromMetrics(
                _wtoi(payload.substr(0, x).c_str()),
                _wtoi(payload.substr(x + 1, bar - x - 1).c_str()),
                payload.substr(bar + 1));
            return S_OK;
        });
    g_state.webView->ExecuteScript(probe, handler.Get());
}

void NavigateConfiguredPage()
{
    if (!g_state.webView) {
        return;
    }

    RefreshResolvedIndex();

    FILETIME writeTime{};
    if (ReadLastWriteTime(g_state.indexPath, writeTime)) {
        const std::wstring url = PathToUrl(g_state.indexPath);
        if (!url.empty()) {
            FILETIME latest{};
            if (!ReadWebDirLatestWrite(DirectoryOf(g_state.indexPath), latest)) {
                latest = writeTime;
            }
            g_state.lastWrite = latest;
            g_state.lastWriteValid = true;
            g_state.currentPageIsFile = true;
            const HRESULT result = g_state.webView->Navigate(url.c_str());
            DesignerLog::Write(
                "PREVIEW navigate_file hr=" + HResultText(result) +
                " path=\"" + DesignerLog::ToUtf8(g_state.indexPath) + "\"");
            return;
        }
    }

    const std::wstring builtIn = BuildIndexPath();
    FILETIME builtInTime{};
    if (_wcsicmp(builtIn.c_str(), g_state.indexPath.c_str()) != 0 &&
        ReadLastWriteTime(builtIn, builtInTime)) {
        const std::wstring url = PathToUrl(builtIn);
        if (!url.empty()) {
            g_state.indexPath = builtIn;
            g_state.lastWrite = builtInTime;
            g_state.lastWriteValid = true;
            g_state.currentPageIsFile = true;
            const HRESULT result = g_state.webView->Navigate(url.c_str());
            DesignerLog::Write(
                "PREVIEW navigate_builtin hr=" + HResultText(result) +
                " path=\"" + DesignerLog::ToUtf8(builtIn) + "\"");
            return;
        }
    }

    g_state.currentPageIsFile = false;
    g_state.lastWriteValid = false;
    constexpr const wchar_t* fallback = LR"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<style>
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;background:#f5f7fb;font-family:"Microsoft YaHei",sans-serif;color:#172033}.card{width:min(680px,88%);padding:42px;border:1px solid #dce3ef;border-radius:18px;background:#fff;box-shadow:0 16px 45px rgba(28,49,87,.10)}h1{margin:0 0 14px;font-size:28px;color:#2563eb}p{margin:8px 0;line-height:1.8}.path{padding:12px 14px;border-radius:10px;background:#f0f5ff;font-family:Consolas,monospace;color:#1d4ed8;margin:6px 0}
</style>
</head>
<body><main class="card"><h1>JadeDesigner 已接入易语言 5.95</h1><p>预览容器和 WebView2 已正常工作。</p><p>把网页放到当前工程目录的 web 子目录（推荐），或放到下面的内置位置，保存后会自动刷新：</p><div class="path">工程目录\web\index.html</div><div class="path">易语言目录\lib\JadeDesigner\web\index.html</div></main></body>
</html>)HTML";
    const HRESULT result = g_state.webView->NavigateToString(fallback);
    DesignerLog::Write("PREVIEW navigate_fallback hr=" + HResultText(result));
}

void PollFileChanges()
{
    if (!g_state.webView) {
        return;
    }

    if (g_state.fitPending) {
        g_state.fitPending = false;
        RunFitToWindow();
    }

    if (RefreshResolvedIndex()) {
        DesignerLog::Write("PREVIEW project_web_switched");
        NavigateConfiguredPage();
        return;
    }

    FILETIME current{};
    const bool exists =
        ReadWebDirLatestWrite(DirectoryOf(g_state.indexPath), current);
    if (!exists) {
        if (g_state.currentPageIsFile) {
            DesignerLog::Write("PREVIEW web sources removed; showing fallback page");
            NavigateConfiguredPage();
        }
        return;
    }

    if (!g_state.currentPageIsFile) {
        DesignerLog::Write("PREVIEW web sources appeared; switching to file page");
        NavigateConfiguredPage();
        return;
    }

    if (!g_state.lastWriteValid || CompareFileTime(&current, &g_state.lastWrite) != 0) {
        g_state.lastWrite = current;
        g_state.lastWriteValid = true;
        const HRESULT result = g_state.webView->Reload();
        DesignerLog::Write("PREVIEW hot_reload hr=" + HResultText(result));
    }
}

void InstallUiEventBridge()
{
    if (!g_state.webView) {
        return;
    }
    const std::wstring script = LR"JS(
(() => {
  if (window.__jadeDesignerEventBridgeInstalled) return 'already-installed';
  window.__jadeDesignerEventBridgeInstalled = true;
  const encode = value => encodeURIComponent(String(value ?? ''));
  window.__jadeDesignerPreview = true;
  let toolsObserve = (element,wire) => wire;
  let toolsIgnored = () => {};
  // Icon-only buttons (<button onclick="closeWindow()"><i class="fa fa-times"></i></button>)
  // have no id, no name and no text, so there is nothing stable to name a
  // subroutine after - except the function their inline handler calls, which is
  // exactly as stable as an id and already says what the button does.
  const inlineHandlerName = element => {
    for (const attribute of ['onclick', 'onchange', 'oninput']) {
      const source = element.getAttribute(attribute);
      if (!source) continue;
      const match = /([A-Za-z_$][\w$]*)\s*\(/.exec(source);
      if (match) return match[1];
    }
    return '';
  };
  const stableName = element => element.dataset.jadeName || element.id || element.name ||
    element.getAttribute('aria-label') || element.title || inlineHandlerName(element);
  const readableName = element => {
    const explicitName = stableName(element);
    if (explicitName) return explicitName;
    const text = (element.textContent || element.value || element.tagName || 'Jade控件').trim();
    return text.slice(0, 60) || 'Jade控件';
  };
  const hasReadableName = element => !!stableName(element);
  // Window chrome and modal-dismiss controls are UI plumbing, not business
  // controls. Do not turn them into Jade callback subroutines even when they
  // have an id or an inline handler such as closeQrModal().
  const isWindowControl = element => {
    if (element.closest('.window-controls')) return true;
    const className = typeof element.className === 'string' ? element.className : '';
    const classTokens = className.split(/\s+/).filter(Boolean);
    if (classTokens.some(token =>
      /^(?:close|dismiss|minimize|maximize)(?:-(?:btn|button|modal|dialog|window|control))?$/i.test(token) ||
      /^(?:modal|dialog|browser|window)-(?:close|dismiss)$/i.test(token) ||
      /^browser-card-close$/i.test(token))) {
      return true;
    }
    const identity = `${element.id || ''} ${inlineHandlerName(element)}`.toLowerCase();
    return /(?:^|[\s_-])(close|hide|dismiss|minimize|maximize)(?=[\w-]*?(?:window|modal|dialog|browser|qr))/i.test(identity);
  };
  // Preview environment has no real JadeView jade object; install a stub that
  // records the channel of every jade.invoke call. Page handlers run after our
  // capture listener, so the latest channel is known by the time we emit.
  if (typeof window.jade === 'undefined') {
    const listeners = {};
    window.jade = {
      invoke: (channel, payload) => {
        window.__jadeLastChannel = String(channel || '');
        window.__jadeLastPayload = payload;
        return Promise.resolve('{"jadePreview":true,"channel":' + JSON.stringify(String(channel || '')) + '}');
      },
      on: (eventName, callback) => { listeners[eventName] = callback; return () => {}; },
      off: (eventName) => { delete listeners[eventName]; },
    };
  }
  // A dialog's dismiss button is plumbing, not a business control, but the ones
  // wired up with addEventListener leave nothing behind in the DOM for
  // isWindowControl to match on - btnInfoOk reads exactly like a business
  // button. What gives them away is the effect of the click: the dialog they
  // sit in was on screen when they were pressed and is gone once the page
  // handler has run, and nothing else happened - no channel invoked, no
  // handler declared. A page normally shares one info box between every
  // validation failure it can report, so without this rule a single subroutine
  // ends up standing for a dozen unrelated flows, and one DOM control would
  // have to answer to more than one callback, which it cannot.
  const visibleDialogOf = element => {
    const dialog = element.closest('.modal,[role="dialog"],dialog');
    return dialog && dialog.getClientRects().length > 0 ? dialog : null;
  };
  // A page that does want a callback on such a button says so explicitly with
  // data-jade-handler or data-jade-channel, and keeps it.
  const dismissedDialog = (element, dialog, captured) => {
    if (!dialog || captured) return false;
    const data = element.dataset;
    if (data.jadeHandler || data.jadeChannel || data.jadeEvent || data.jadeCall) return false;
    return dialog.getClientRects().length === 0;
  };
  const emit = (domEvent, element, eventName, controlType, dialog) => {
    const elementId = readableName(element);
    const captured = (window.__jadeLastChannel || '').trim();
    if (dismissedDialog(element, dialog, captured)) {
      toolsIgnored(element,'dialog_dismiss');
      window.chrome.webview.postMessage(
        'JADE_SKIP\t' + [elementId, domEvent, 'dialog_dismiss'].map(encode).join('\t'));
      return;
    }
    const callType = element.dataset.jadeCall ||
      (element.dataset.jadeChannel || captured ? 'JadeView.通讯.订阅' : '');
    const callParam = element.dataset.jadeChannel || element.dataset.jadeEvent || captured;
    const explicitHandler = element.dataset.jadeHandler || '';
    // win:xxx buttons without an id get no readable handler name; the plugin
    // then derives ipc_窗口最小化 from the channel mapping.
    // A channel is only an operation name, not a callback name. Unless the
    // page explicitly supplies one, keep the stable element/event convention.
    const handlerName = explicitHandler ||
      (hasReadableName(element) ? `${elementId}_${eventName}` : '');
    const assemblyName = element.dataset.jadeAssembly || '';
    const value = element.value ?? element.getAttribute('value') ?? '';
    const checked = element.checked === true ? '1' : '0';
    const fields = [domEvent, controlType, elementId, value, checked, handlerName, assemblyName,
      callType, callParam];
    const wire = 'JADE_EVT\t' + fields.map(encode).join('\t');
    window.chrome.webview.postMessage(toolsObserve(element, wire));
  };
  const scheduleEmit = (domEvent, element, eventName, controlType) => {
    if (!element) return;
    window.__jadeLastChannel = '';
    // Taken here, in the capture phase, because the page's own handler has not
    // run yet; by the time the queued emit fires a dismiss button has already
    // closed the dialog and there would be nothing left to compare against.
    const dialog = visibleDialogOf(element);
    // Do not cancel a valid control event merely because the control was hidden
    // or detached by its own click handler (the info/confirm modal does exactly
    // that). Keep the element reference alive until the queued emit runs so the
    // native side receives the click and can create/jump to the callback.
    setTimeout(() => emit(domEvent, element, eventName, controlType, dialog), 0);
  };
  document.addEventListener('click', event => {
    if (window.__jadeTextEditing || (window.__jadeInteractionMode && window.__jadeInteractionMode !== 'event')) return;
    const source = event.target instanceof Element ? event.target : event.target?.parentElement;
    const element = source?.closest('button,[role="button"],input[type="button"],input[type="submit"]');
    if (element && !isWindowControl(element)) {
      // A browser double-click dispatches two click events before dblclick.
      // The first click is the real Jade event; forwarding the second one
      // re-enters the native repair path and can append the callback body again.
      // Leave the page's own click behavior untouched, but do not turn the
      // second click into another code-generation request.
      if (event.detail > 1) {
        toolsIgnored(element,'double_click');
        return;
      }
      scheduleEmit('click', element, '被单击', 'button');
    } else if(element) {
      toolsIgnored(element,'window_control');
    }
  }, true);
  document.addEventListener('change', event => {
    if (window.__jadeTextEditing || (window.__jadeInteractionMode && window.__jadeInteractionMode !== 'event')) return;
    const element = event.target;
    if (!(element instanceof Element)) return;
    if (element.matches('select')) {
      scheduleEmit('change', element, '选择项被改变', 'select');
    } else if (element.matches('input[type="radio"]')) {
      scheduleEmit('change', element, '选中状态被改变', 'radio');
    } else if (element.matches('input[type="checkbox"]')) {
      scheduleEmit('change', element, '选中状态被改变', 'checkbox');
    }
  }, true);
  const commonHost = document.createElement('jade-common-tools');
  commonHost.style.cssText = 'all:initial;position:fixed;bottom:12px;right:12px;width:40px;height:40px;z-index:2147482999;font:14px/1.5 "Segoe UI","Microsoft YaHei",sans-serif;letter-spacing:0;color:#25332d;';
  const commonRoot = commonHost.attachShadow({ mode: 'open' });
  const commonStyle = document.createElement('style');
  commonStyle.textContent = `
    :host { font:14px/1.5 "Segoe UI","Microsoft YaHei",sans-serif;letter-spacing:0;color:#25332d; }
    button { font:inherit;padding:9px 14px;border:1px solid #9daea5;border-radius:6px;
      background:#fff;color:inherit;cursor:pointer;box-shadow:0 2px 8px #0002; }
    button:hover { background:#f0f6f3; } button:disabled { opacity:.6;cursor:wait; }
    button:focus-visible { outline:2px solid #16784b;outline-offset:2px; }
    output { display:block;max-width:360px;box-sizing:border-box;margin-bottom:8px;
      padding:12px;background:#fff;border:1px solid #9daea5;border-radius:6px;overflow-wrap:anywhere; }
    output:empty { display:none; } output[data-failed] { border-color:#b34e42;color:#8f2920; }
    fieldset { border:0;margin:0;padding:0;min-width:0; }
    label { display:flex;align-items:center;gap:8px;margin:8px 0; }
    input[type=checkbox] { width:17px;height:17px;accent-color:#16784b; }
    input[type=text] { box-sizing:border-box;width:100%;min-width:0;padding:7px;border:1px solid #9daea5;
      border-radius:4px;font:inherit;color:inherit;background:#fff; }
  `;
  const commonButton = document.createElement('button');
  commonButton.type = 'button'; commonButton.textContent = '生成公共代码';
  commonButton.title = '创建 JadeView 基础程序集，保留已有启动入口';
  const commonStatus = document.createElement('output');
  commonStatus.setAttribute('role', 'status'); commonStatus.setAttribute('aria-live', 'polite');
  const commonFields = document.createElement('fieldset');
  const commonLegend = document.createElement('legend'); commonLegend.textContent = '公共代码设置';
  commonFields.append(commonLegend);
  const commonCheck = (name, title) => {
    const label = document.createElement('label'), input = document.createElement('input');
    input.type = 'checkbox'; input.name = name; label.append(input, title); commonFields.append(label); return input;
  };
  const commonTray = commonCheck('tray', '托盘常驻');
  commonTray.title = '需要类_json、程序目录 app.ico，以及 JadeView DLL 2.3 或更新';
  const commonSingle = commonCheck('singleInstance', '单实例');
  commonSingle.title = '再次启动时恢复已有主窗口；需要 JadeView DLL 2.3 或更新';
  const commonIdLabel = document.createElement('label'); commonIdLabel.textContent = '应用标识';
  const commonId = document.createElement('input'); commonId.type = 'text'; commonId.name = 'appId';
  commonId.id = 'common-app-id'; commonId.maxLength = 64; commonIdLabel.htmlFor = commonId.id;
  commonId.placeholder = '按当前工程自动生成';
  commonId.title = '6～64 位英文字母、数字、下划线或短横线。不同软件不要共用标识。';
  commonFields.append(commonIdLabel, commonId);
  // A separate command, never a business-control callback. Shadow DOM keeps page
  // selectors/styles and the document's event-to-code listener out of this tool.
  commonRoot.append(commonStyle);
  document.documentElement.append(commonHost);
)JS" + DesignerToolsScript() + LR"JS(
  let noticeHost = null;
  let noticeText = null;
  let noticeTimer = null;
  const showCreationNotice = (text, failed = false) => {
    if (!text) return;
    if (!noticeHost?.isConnected) {
      noticeHost = document.createElement('jade-created-notice');
      noticeHost.style.cssText = 'all:initial;position:fixed;top:16px;right:16px;z-index:2147483000;pointer-events:none;max-width:calc(100vw - 32px);';
      const root = noticeHost.attachShadow({ mode: 'open' });
      const style = document.createElement('style');
      style.textContent = `
        :host { pointer-events:none; }
        .notice { display:flex;align-items:flex-start;gap:10px;box-sizing:border-box;
          max-width:440px;padding:13px 17px;border:1px solid #b4d2c0;border-radius:6px;
          background:#f3fcf6;color:#195d39;box-shadow:0 4px 18px #123c2422;
          font:15px/1.6 "Segoe UI","Microsoft YaHei",sans-serif;letter-spacing:0;
          pointer-events:none;opacity:0;visibility:hidden;transition:opacity 160ms ease; }
        :host([visible]) .notice { opacity:1;visibility:visible; }
        :host([data-failed]) .notice { color:#aa382d;background:#fff5f4;border-color:#dab6b1; }
        .check { flex-shrink:0;font-size:18px;line-height:24px; }
        .text { min-width:0;overflow-wrap:anywhere; }
        @media(prefers-reduced-motion:reduce) { .notice { transition:none; } }
      `;
      const panel = document.createElement('div');
      panel.className = 'notice';
      panel.setAttribute('role', 'status');
      panel.setAttribute('aria-live', 'polite');
      panel.setAttribute('aria-atomic', 'true');
      const check = document.createElement('span');
      check.className = 'check'; check.textContent = '\u2713'; check.setAttribute('aria-hidden', 'true');
      noticeText = document.createElement('span'); noticeText.className = 'text';
      panel.append(check, noticeText); root.append(style, panel);
      document.documentElement.append(noticeHost);
    }
    clearTimeout(noticeTimer);
    noticeText.textContent = text;
    noticeHost.toggleAttribute('data-failed', failed);
    noticeHost.shadowRoot.querySelector('.check').textContent = failed ? '!' : '\u2713';
    noticeHost.setAttribute('visible', '');
    noticeTimer = setTimeout(() => noticeHost?.removeAttribute('visible'), failed ? 6000 : 3000);
  };
  window.chrome.webview.addEventListener('message', event => {
    if (typeof event.data !== 'string') return;
    if (event.data.startsWith('JADE_TOOL_RESULT\t') || event.data.startsWith('JADE_TRACE_RESULT\t') || event.data.startsWith('JADE_NATIVE_TOOLBOX\t')) return;
    const commonPrefix = 'JADE_COMMON_RESULT\t';
    if (event.data.startsWith(commonPrefix)) {
      const reply = event.data.slice(commonPrefix.length);
      commonButton.disabled = false; commonButton.textContent = '预览公共代码';
      commonFields.disabled = false;
      commonStatus.toggleAttribute('data-failed', !reply.startsWith('1\t'));
      commonStatus.textContent = reply.slice(2);
      return;
    }
    const noticePrefix = 'JADE_NOTICE\t';
    if (event.data.startsWith(noticePrefix)) {
      showCreationNotice(event.data.slice(noticePrefix.length));
      return;
    }
    const status = document.querySelector('[data-jade-status]') || document.getElementById('eventLog');
    if (status) status.textContent = event.data;
  });
  return 'installed';
})()
)JS";
    const HRESULT result = g_state.webView->ExecuteScript(script.c_str(), nullptr);
    DesignerLog::Write("PREVIEW install_ui_event_bridge hr=" + HResultText(result));
}

// The bridge reports the controls it deliberately declined to turn into an
// event, so that a click which is meant to do nothing can be told apart in the
// log from one that went missing.
bool LogIgnoredControl(const std::wstring& wireMessage)
{
    constexpr wchar_t kPrefix[] = L"JADE_SKIP\t";
    constexpr size_t kPrefixLength = (sizeof(kPrefix) / sizeof(wchar_t)) - 1;
    if (wireMessage.compare(0, kPrefixLength, kPrefix) != 0) {
        return false;
    }
    std::vector<std::string> fields;
    const std::wstring_view body = std::wstring_view(wireMessage).substr(kPrefixLength);
    for (size_t start = 0; start <= body.size();) {
        const size_t separator = body.find(L'\t', start);
        const size_t stop = separator == std::wstring_view::npos ? body.size() : separator;
        fields.push_back(IdeEventRouter::DecodeWireField(body.substr(start, stop - start)));
        if (separator == std::wstring_view::npos) {
            break;
        }
        start = separator + 1;
    }
    const auto field = [&fields](size_t index, const char* fallback) {
        return index < fields.size() && !fields[index].empty() ? fields[index] : fallback;
    };
    DesignerLog::Write(
        "UI_EVENT control_ignored control=\"" + field(0, "?") +
        "\" dom=" + field(1, "?") + " reason=" + field(2, "unspecified"));
    return true;
}

std::wstring ToolEncode(std::string_view text)
{
    constexpr wchar_t hex[]=L"0123456789ABCDEF";
    std::wstring out;
    for(unsigned char c:text) { out+=L'%'; out+=hex[c>>4]; out+=hex[c&15]; }
    return out;
}

// 1: available, 0: taken, -1: unverified. New controls must not silently adopt
// an orphaned callback or an already wired channel from an older HTML page.
int VisualNameAvailable(const std::wstring& kind,const std::wstring& number,std::string& error)
{
    const auto name=DesignerVisual::RoutineName(kind,number);if(name.empty())return 1;
    std::string routine,assembly;
    const auto read=HookBridge::ReadRoutine(WideToAnsi(name.c_str()),routine,error);
    if(read<0)return -1;if(read>0)return 0;
    const auto assemblyRead=HookBridge::ReadAssembly(WideToAnsi(L"Jade_通讯_订阅集"),assembly,error);
    if(assemblyRead<0)return -1;
    const auto channel=DesignerText::Utf8(L"ui:jade_"+kind+L"_"+number);
    std::istringstream lines(assembly);std::string line;
    while(std::getline(lines,line)){std::string candidate,target;if(DesignerInspection::Subscription(line,candidate,target)&&candidate==channel)return 0;}
    return 1;
}

void ProcessDesignerTool(const std::wstring& wire)
{
    std::vector<std::wstring> fields;
    size_t p=0;
    while(p<=wire.size()) {
        const auto end=wire.find(L'\t',p);
        fields.push_back(DesignerText::Wide(IdeEventRouter::DecodeWireField(std::wstring_view(wire).substr(p,end==wire.npos?wire.size()-p:end-p))));
        if(end==wire.npos) break; p=end+1;
    }
    if(fields.size()<3 || fields.size()>12) return;
    const auto id=fields[1], action=fields[2];
    if(id.empty() || id.size()>16 || id.find_first_not_of(L"0123456789")!=id.npos) return;
    const auto reply=[&](bool ok,std::initializer_list<std::string> values) {
        std::wstring out=L"JADE_TOOL_RESULT\t"+id+(ok?L"\t1":L"\t0");
        for(const auto& value:values) out+=L"\t"+ToolEncode(value);
        if(g_state.webView) g_state.webView->PostWebMessageAsString(out.c_str());
    };
    try {
        if(action==L"visual_toolbox"&&fields.size()==5) {
            if((fields[3]!=L"design"&&fields[3]!=L"event"&&fields[3]!=L"preview") ||
                (fields[4]!=L"1"&&fields[4]!=L"0")){reply(false,{"无效的组件箱设置"});return;}
            g_visualDesignMode=fields[3]==L"design";
            std::wstring error;
            g_nativeToolbox.SetEditable(g_visualDesignMode);
            const bool ready=g_nativeToolbox.Configure(fields[4]==L"1",WebPreview::IsActive(),error);
            DesignerLog::Write("PREVIEW native_toolbox enabled="+std::string(fields[4]==L"1"?"1":"0")+" ready="+(ready?"1":"0")+" floating="+(g_nativeToolbox.IsFloating()?"1":"0"));
            reply(ready,{ready?"组件箱设置已应用":DesignerText::Utf8(error)});
        } else if(action==L"copy_description"&&fields.size()==4) {
            const auto& text=fields[3];
            if(text.empty()||text.size()>32768||text.find(L'\0')!=text.npos){reply(false,{"元素描述过长或内容无效"});return;}
            HGLOBAL data=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
            auto* buffer=data?static_cast<wchar_t*>(GlobalLock(data)):nullptr;
            if(!buffer){if(data)GlobalFree(data);reply(false,{"无法分配复制缓冲区"});return;}
            std::wmemcpy(buffer,text.c_str(),text.size()+1);GlobalUnlock(data);
            if(!OpenClipboard(g_state.hostWindow)){GlobalFree(data);reply(false,{"剪贴板正忙，请重试复制"});return;}
            const bool copied=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,data);
            CloseClipboard();if(!copied)GlobalFree(data);
            reply(copied,{copied?"元素描述已复制":"复制失败，请重试"});
        } else if(action==L"visual_name"&&fields.size()==5) {
            if(DesignerVisual::Standard(fields[3],fields[4]).empty()){reply(false,{"无效的控件类型或编号"});return;}
            auto number=std::stoul(fields[4]);std::string error;
            if(DesignerVisual::RoutineName(fields[3],fields[4]).empty()){reply(true,{std::to_string(number)});return;}
            std::string existing;
            if(HookBridge::ReadAssembly(WideToAnsi(L"Jade_通讯_订阅集"),existing,error)<0){reply(false,{"无法核验已有订阅："+error});return;}
            std::set<std::string> names,channels;
            for(const auto& routine:DesignerInspection::Routines(existing))names.insert(routine.name);
            std::istringstream lines(existing);std::string line;
            while(std::getline(lines,line)){std::string channel,target;if(DesignerInspection::Subscription(line,channel,target))channels.insert(channel);}
            int probes=0;
            for(;number<=999999;++number) {
                const auto n=std::to_wstring(number),name=DesignerVisual::RoutineName(fields[3],n);
                if(names.contains(DesignerText::Utf8(name))||channels.contains(DesignerText::Utf8(L"ui:jade_"+fields[3]+L"_"+n)))continue;
                if(++probes>32)break;
                std::string routine;
                const auto found=HookBridge::ReadRoutine(WideToAnsi(name.c_str()),routine,error);
                if(found<0){reply(false,{"无法核验工程内已有回调，未新增控件："+error});return;}
                if(found==0){reply(true,{std::to_string(number)});return;}
            }
            reply(false,{"连续多个编号已被占用，未新增控件"});
        } else if(action==L"health" && fields.size()==3) {
            auto checks=IdeEventRouter::InspectProjectHealth();
            std::wstring project;
            if(ReadRecentProjectPath(project)) {
                const auto directory=DirectoryOf(project);
                for(const auto* name:{L"web\\index.html",L"jadeview_x86.dll",L"app.ico"}) {
                    const auto attrs=GetFileAttributesW((directory+L"\\"+name).c_str());
                    const bool found=attrs!=INVALID_FILE_ATTRIBUTES&&!(attrs&FILE_ATTRIBUTE_DIRECTORY);
                    checks.push_back({found?"ok":"warning",DesignerText::Utf8(name),found?"工程目录文件存在；版本、内容及实际运行目录未核验":"工程目录未找到；若输出到其他目录请在实际运行目录核对（app.ico 仅托盘需要）"});
                }
            } else checks.push_back({"unknown","工程目录","未取得已保存工程路径"});
            std::string report;
            for(auto check:checks) {
                for(auto* value:{&check.title,&check.detail})for(char& c:*value)if(c=='\t'||c=='\r'||c=='\n')c=' ';
                report+=check.state+"\t"+check.title+"\t"+check.detail+"\n";
            }
            reply(true,{report});
        } else if(action==L"install_diagnostics" && fields.size()==3) {
            std::wstring resolved;std::string message;
            if(!ResolveProjectWebIndex(resolved)||resolved!=g_state.indexPath){reply(false,{"请先打开工程对应网页"});return;}
            const bool ok=DiagnosticsInstall::Install(resolved,RuntimeDiagnosticsScript(),message);
            reply(ok,{message});
        } else if((action==L"inspect" || action==L"repair" || action==L"locate") && fields.size()==4) {
            IdeEventRouter::UiEvent event;
            if(!IdeEventRouter::TryParseWebMessage(fields[3],event)) { reply(false,{"无效的控件信息"}); return; }
            if(action==L"inspect") {
                const auto state=IdeEventRouter::InspectBinding(event);
                reply(true,{state.status,state.message,state.normalized.assemblyName,state.normalized.handlerName,state.normalized.callParam});
            } else {
                const auto result=IdeEventRouter::OperateBinding(g_state.mainWindow,g_state.mdiClient,event,action==L"locate");
                reply(result.succeeded,{result.message});
            }
        } else if(action==L"common_preview" && fields.size()==6) {
            CommonCode::Options options;
            if(!CommonCode::ParseCommand(L"JADE_COMMAND\tgenerate_common\t"+fields[3]+L"\t"+fields[4]+L"\t"+fields[5],options)) {
                reply(false,{"公共代码选项无效"}); return;
            }
            g_commonPreview=IdeEventRouter::PreviewCommonCode(g_state.mainWindow,options);
            ReadRecentProjectPath(g_commonPreviewProject);
            ++g_commonPreviewRevision;
            reply(true,{std::to_string(g_commonPreviewRevision),g_commonPreview.ready?"1":"0",g_commonPreview.report,g_commonPreview.source,g_commonPreview.existing});
        } else if(action==L"common_apply" && fields.size()==4) {
            std::wstring project;
            ReadRecentProjectPath(project);
            if(project.empty() || project!=g_commonPreviewProject || fields[3]!=std::to_wstring(g_commonPreviewRevision) || !g_commonPreview.ready) { reply(false,{"预览已过期，请重新检查"}); return; }
            const auto now=IdeEventRouter::PreviewCommonCode(g_state.mainWindow,g_commonPreview.options);
            g_commonPreview.ready=false;
            if(!now.ready || now.source!=g_commonPreview.source || now.existing!=g_commonPreview.existing || now.report!=g_commonPreview.report) {
                reply(false,{"工程或模块已改变，请重新预览；未写入代码"}); return;
            }
            const auto result=IdeEventRouter::GenerateCommonCode(g_state.mainWindow,now.options);
            reply(result.succeeded,{result.message});
        } else if(action==L"text_open" || action==L"text_save" || action==L"text_undo" || action==L"visual_save") {
            std::wstring resolved; std::string error;
            if(!ResolveProjectWebIndex(resolved) || resolved!=g_state.indexPath) { reply(false,{"仅支持当前工程 web 中的本地 HTML，不修改内置页面"}); return; }
            if(action==L"text_open") {
                if(!g_textDocument.Open(resolved,error)) { reply(false,{error}); return; }
            } else {
                if(g_textDocument.path!=resolved) { reply(false,{"工程或页面已切换，请重新选择文字"}); return; }
                bool ok=false;
                if(action==L"text_undo") ok=g_textDocument.Undo(error);
                else if(action==L"visual_save"&&fields.size()==5) {
                    const auto number=[](const std::wstring& value) {return !value.empty()&&value.size()<=10&&value.find_first_not_of(L"0123456789")==value.npos;};
                    if(!number(fields[3])||fields[4].size()>512*1024){reply(false,{"无效的控件修改请求"});return;}
                    std::vector<DesignerVisual::Edit> edits;
                    size_t lineStart=0;
                    while(lineStart<fields[4].size()) {
                        auto lineEnd=fields[4].find(L'\n',lineStart);if(lineEnd==std::wstring::npos)lineEnd=fields[4].size();
                        const auto line=fields[4].substr(lineStart,lineEnd-lineStart);std::vector<std::wstring> values;
                        for(size_t start=0;start<=line.size();) {
                            auto end=line.find(L'\t',start);if(end==line.npos)end=line.size();
                            values.push_back(DesignerText::Wide(IdeEventRouter::DecodeWireField(std::wstring_view(line).substr(start,end-start))));
                            if(end==line.size())break;start=end+1;
                        }
                        if(values.size()!=6||!number(values[1])||!number(values[2])||edits.size()>=32){reply(false,{"无效的控件修改范围"});return;}
                        edits.push_back({values[0],std::stoul(values[1]),std::stoul(values[2]),values[3],values[4],values[5]});
                        lineStart=lineEnd+1;
                    }
                    for(const auto& edit:edits)if(edit.kind==L"insert") {
                        if(DesignerVisual::Standard(edit.key,edit.value).empty()){reply(false,{"无效的新增控件"});return;}
                        if(VisualNameAvailable(edit.key,edit.value,error)!=1){reply(false,{"回调名或频道已占用，或内存核验失败，请重新添加："+error});return;}
                    }
                    ok=DesignerVisual::Save(g_textDocument,static_cast<unsigned>(std::stoul(fields[3])),edits,error);
                }
                else if(fields.size()==8) {
                    for(int i:{3,4,5}) if(fields[i].empty()||fields[i].size()>10||fields[i].find_first_not_of(L"0123456789")!=fields[i].npos) { reply(false,{"无效的文字位置"}); return; }
                    ok=g_textDocument.Save(static_cast<unsigned>(std::stoul(fields[3])),std::stoul(fields[4]),std::stoul(fields[5]),fields[6],fields[7],error);
                }
                if(!ok) { reply(false,{error.empty()?"文字修改失败":error}); return; }
                ReadWebDirLatestWrite(DirectoryOf(g_state.indexPath),g_state.lastWrite);
                g_state.lastWriteValid=true;
                if(action==L"text_undo" && g_state.webView) g_state.webView->Reload();
                if(action==L"visual_save") g_nativeToolbox.Pointer();
            }
            reply(true,{std::to_string(g_textDocument.revision),g_textDocument.bytes,DesignerText::Utf8(resolved)});
        } else reply(false,{"不支持的管理命令"});
    } catch(const std::exception&) { reply(false,{"管理操作失败，未继续执行"}); }
}

HRESULT OnWebMessageReceived(
    unsigned long long generation,
    ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
{
    if (g_state.shuttingDown || generation != g_state.generation) {
        return S_OK;
    }
    LPWSTR message = nullptr;
    const HRESULT readResult = args != nullptr
        ? args->TryGetWebMessageAsString(&message)
        : E_POINTER;
    if (FAILED(readResult) || message == nullptr) {
        DesignerLog::Write(
            "UI_EVENT web_message_read_failed hr=" + HResultText(readResult));
        if (message != nullptr) {
            CoTaskMemFree(message);
        }
        return S_OK;
    }

    const std::wstring wireMessage(message);
    CoTaskMemFree(message);
    if(wireMessage.size()>4*1024*1024) return S_OK;
    LPWSTR origin=nullptr;
    const HRESULT sourceResult=args->get_Source(&origin);
    const std::wstring sourceUrl=origin?origin:L"";
    if(origin) CoTaskMemFree(origin);
    const std::wstring expectedUrl=PathToUrl(g_state.indexPath);
    if(FAILED(sourceResult) || !DesignerText::SameDocumentUrl(sourceUrl,expectedUrl)) return S_OK;
    if (LogIgnoredControl(wireMessage)) {
        return S_OK;
    }
    IdeEventRouter::UiEvent event;
    CommonCode::Options commonOptions;
    const bool commonCommand = CommonCode::ParseCommand(wireMessage, commonOptions);
    const bool toolCommand=wireMessage.starts_with(L"JADE_TOOL\t");
    if (!commonCommand && wireMessage.find(L"JADE_COMMAND\tgenerate_common") == 0) {
        if (g_state.webView) g_state.webView->PostWebMessageAsString(
            L"JADE_COMMON_RESULT\t0\t公共代码选项无效，未写入工程。");
        return S_OK;
    }
    if (!commonCommand && !toolCommand && !IdeEventRouter::TryParseWebMessage(wireMessage, event)) {
        // Not one of the bridge's messages at all (the page may post its own).
        // Anything the bridge sent now reaches Route, which reports what it
        // cannot do rather than leaving the click unanswered.
        DesignerLog::Write(
            "UI_EVENT ignored not_a_jade_event len=" +
            std::to_string(wireMessage.size()));
        return S_OK;
    }

    if (g_state.hostWindow == nullptr || !IsWindow(g_state.hostWindow)) {
        DesignerLog::Write("UI_EVENT queue_failed reason=preview_window_unavailable");
        return S_OK;
    }
    auto* pending = new (std::nothrow) PendingUiEvent{};
    if (pending == nullptr) {
        DesignerLog::Write("UI_EVENT queue_failed reason=out_of_memory");
        return S_OK;
    }
    pending->generation = generation;
    pending->documentGeneration = g_state.documentGeneration;
    pending->event = std::move(event);
    pending->commonCommand = commonCommand;
    pending->commonOptions = std::move(commonOptions);
    pending->toolMessage=toolCommand?wireMessage:L"";
    ReadRecentProjectPath(pending->projectPath);
    pending->indexPath=g_state.indexPath;
    if(!toolCommand&&!commonCommand&&std::count(wireMessage.begin(),wireMessage.end(),L'\t')==10) {
        const auto id=wireMessage.substr(wireMessage.find_last_of(L'\t')+1);
        if(!id.empty()&&id.size()<=16&&id.find_first_not_of(L"0123456789")==id.npos)pending->traceId=id;
    }
    if (!PostMessageW(
            g_state.hostWindow,
            kRouteUiEventMessage,
            0,
            reinterpret_cast<LPARAM>(pending))) {
        delete pending;
        DesignerLog::Write(
            "UI_EVENT queue_failed reason=post_message error=" +
            std::to_string(GetLastError()));
        return S_OK;
    }
    DesignerLog::Write("UI_EVENT queued route=preview_window");
    return S_OK;
}

void ProcessQueuedUiEvent(PendingUiEvent* rawPending)
{
    std::unique_ptr<PendingUiEvent> pending(rawPending);
    if (!pending || g_state.shuttingDown ||
        pending->generation != g_state.generation ||
        pending->documentGeneration != g_state.documentGeneration ||
        g_state.mainWindow == nullptr || g_state.mdiClient == nullptr ||
        !IsWindow(g_state.mainWindow) || !IsWindow(g_state.mdiClient)) {
        DesignerLog::Write("UI_EVENT dropped queued_event=stale_or_invalid_context");
        return;
    }

    std::wstring currentProject;
    ReadRecentProjectPath(currentProject);
    if(pending->projectPath!=currentProject || pending->indexPath!=g_state.indexPath) return;
    if(!pending->toolMessage.empty()) { ProcessDesignerTool(pending->toolMessage); return; }

    IdeEventRouter::RouteResult result{
        false, "exception", "UI event processing failed"};
    try {
        result = pending->commonCommand
        ? IdeEventRouter::GenerateCommonCode(g_state.mainWindow, pending->commonOptions)
            : IdeEventRouter::Route(g_state.mainWindow, g_state.mdiClient, pending->event);
    }
    catch (const std::exception& error) {
        result = {false, "exception", std::string("UI事件处理失败：") + error.what()};
        DesignerLog::Write(
            "UI_EVENT route_exception what=\"" + std::string(error.what()) + "\"");
    }
    catch (...) {
        result = {false, "exception", "UI event processing exception: unknown"};
        DesignerLog::Write("UI_EVENT route_exception what=unknown");
    }

    DesignerLog::Write(
        "UI_EVENT result success=" + std::to_string(result.succeeded ? 1 : 0) +
        " action=" + result.action + " message=\"" + result.message + "\"");
    if (g_state.webView && pending->generation==g_state.generation && pending->documentGeneration==g_state.documentGeneration) {
        if (pending->commonCommand) {
            const std::wstring reply = std::wstring(L"JADE_COMMON_RESULT\t") +
                (result.succeeded ? L"1\t" : L"0\t") + IdeEventRouter::BuildAckMessage(result);
            g_state.webView->PostWebMessageAsString(reply.c_str());
            return;
        }
        const std::wstring acknowledgement = IdeEventRouter::BuildAckMessage(result);
        g_state.webView->PostWebMessageAsString(acknowledgement.c_str());
        if(!pending->traceId.empty()) {
            const auto trace=L"JADE_TRACE_RESULT\t"+pending->traceId+(result.succeeded?L"\t1\t":L"\t0\t")+ToolEncode(result.action);
            g_state.webView->PostWebMessageAsString(trace.c_str());
        }
        if (result.succeeded && result.action == "create_background" && !result.notice.empty()) {
            const std::wstring notice = L"JADE_NOTICE\t" + result.notice;
            const HRESULT sent = g_state.webView->PostWebMessageAsString(notice.c_str());
            DesignerLog::Write("PREVIEW creation_notice hr=" + HResultText(sent));
        }
    }
}

HRESULT OnNavigationCompleted(
    unsigned long long generation,
    ICoreWebView2*,
    ICoreWebView2NavigationCompletedEventArgs* args)
{
    if (g_state.shuttingDown || generation != g_state.generation) {
        return S_OK;
    }
    BOOL succeeded = FALSE;
    COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
    const HRESULT successResult = args != nullptr ? args->get_IsSuccess(&succeeded) : E_POINTER;
    const HRESULT statusResult = args != nullptr ? args->get_WebErrorStatus(&status) : E_POINTER;
    DesignerLog::Write(
        "PREVIEW navigation_completed success_hr=" + HResultText(successResult) +
        " status_hr=" + HResultText(statusResult) +
        " success=" + std::to_string(succeeded ? 1 : 0) +
        " web_error_status=" + std::to_string(static_cast<int>(status)));
    if (succeeded != FALSE) {
        InstallUiEventBridge();
        RunFitToWindow();
        g_state.fitPending = true; // re-check once after late layout/font settle
    }
    return S_OK;
}

HRESULT OnControllerCreated(
    unsigned long long generation,
    HRESULT result,
    ICoreWebView2Controller* controller)
{
    if (g_state.shuttingDown || generation != g_state.generation) {
        return S_OK;
    }
    g_state.webViewStarting = false;
    DesignerLog::Write("PREVIEW controller_callback hr=" + HResultText(result));
    if (FAILED(result) || controller == nullptr || g_state.hostWindow == nullptr) {
        return S_OK;
    }

    g_state.controller = controller;
    const HRESULT webViewResult = g_state.controller->get_CoreWebView2(&g_state.webView);
    DesignerLog::Write("PREVIEW get_CoreWebView2 hr=" + HResultText(webViewResult));
    if (FAILED(webViewResult) || !g_state.webView) {
        return S_OK;
    }

    ResizeController();
    g_state.controller->put_IsVisible(g_state.active ? TRUE : FALSE);

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(g_state.webView->get_Settings(&settings)) && settings) {
        settings->put_IsScriptEnabled(TRUE);
        settings->put_IsWebMessageEnabled(TRUE);
        settings->put_AreDefaultScriptDialogsEnabled(TRUE);
        settings->put_AreDevToolsEnabled(TRUE);
    }

    auto webMessageHandler = Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [generation](ICoreWebView2* view, ICoreWebView2WebMessageReceivedEventArgs* args) {
            return OnWebMessageReceived(generation, view, args);
        });
    const HRESULT webMessageResult = g_state.webView->add_WebMessageReceived(
        webMessageHandler.Get(), &g_state.webMessageToken);
    g_state.webMessageTokenValid = SUCCEEDED(webMessageResult);
    DesignerLog::Write(
        "PREVIEW add_WebMessageReceived hr=" + HResultText(webMessageResult));
    DesignerLog::Write("PREVIEW ui_event_bridge_build=15 designer_tools=1 health=1 diagnostics=1 compact_tools=1 visual_design=1 native_toolbox=1 pixel_nudge=1 element_description=1 floating_toolbox=1 explicit_design_panel=1");

    auto startingHandler = Callback<ICoreWebView2NavigationStartingEventHandler>(
        [generation](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*) -> HRESULT {
            if(generation==g_state.generation) {
                ++g_state.documentGeneration;
                g_commonPreview.ready=false;
                g_visualDesignMode=false;
                PostMessageW(g_state.hostWindow,kNativeToolboxMessage,0,0);
            }
            return S_OK;
        });
    g_state.navigationStartingTokenValid=SUCCEEDED(g_state.webView->add_NavigationStarting(
        startingHandler.Get(),&g_state.navigationStartingToken));

    auto navigationHandler = Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [generation](ICoreWebView2* view, ICoreWebView2NavigationCompletedEventArgs* args) {
            return OnNavigationCompleted(generation, view, args);
        });
    const HRESULT eventResult = g_state.webView->add_NavigationCompleted(
        navigationHandler.Get(), &g_state.navigationToken);
    g_state.navigationTokenValid = SUCCEEDED(eventResult);
    DesignerLog::Write("PREVIEW add_NavigationCompleted hr=" + HResultText(eventResult));
    NavigateConfiguredPage();
    InvalidateRect(g_state.hostWindow, nullptr, TRUE);
    return S_OK;
}

HRESULT OnEnvironmentCreated(
    unsigned long long generation,
    HRESULT result,
    ICoreWebView2Environment* environment)
{
    if (g_state.shuttingDown || generation != g_state.generation) {
        return S_OK;
    }
    DesignerLog::Write("PREVIEW environment_callback hr=" + HResultText(result));
    if (FAILED(result) || environment == nullptr || g_state.hostWindow == nullptr) {
        g_state.webViewStarting = false;
        return S_OK;
    }

    g_state.environment = environment;
    auto controllerHandler = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [generation](HRESULT result, ICoreWebView2Controller* controller) {
            return OnControllerCreated(generation, result, controller);
        });
    const HRESULT createResult = g_state.environment->CreateCoreWebView2Controller(
        g_state.hostWindow, controllerHandler.Get());
    DesignerLog::Write("PREVIEW create_controller_start hr=" + HResultText(createResult));
    if (FAILED(createResult)) {
        g_state.webViewStarting = false;
    }
    return S_OK;
}

void EnsureWebView()
{
    if (g_state.shuttingDown || g_state.webView || g_state.webViewStarting ||
        g_state.hostWindow == nullptr) {
        return;
    }

    LPWSTR version = nullptr;
    const HRESULT versionResult = GetAvailableCoreWebView2BrowserVersionString(nullptr, &version);
    DesignerLog::Write(
        "PREVIEW runtime_query hr=" + HResultText(versionResult) +
        " version=\"" + (version != nullptr ? DesignerLog::ToUtf8(version) : std::string()) + "\"");
    if (version != nullptr) {
        CoTaskMemFree(version);
    }
    if (FAILED(versionResult)) {
        InvalidateRect(g_state.hostWindow, nullptr, TRUE);
        return;
    }

    if (!g_state.comAttempted) {
        g_state.comAttempted = true;
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        g_state.comNeedsUninitialize = SUCCEEDED(comResult);
        DesignerLog::Write("PREVIEW CoInitializeEx hr=" + HResultText(comResult));
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
            return;
        }
    }

    g_state.webViewStarting = true;
    const unsigned long long generation = g_state.generation;
    const std::wstring userDataFolder = BuildUserDataFolder();
    auto environmentHandler = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [generation](HRESULT result, ICoreWebView2Environment* environment) {
            return OnEnvironmentCreated(generation, result, environment);
        });
    const HRESULT createResult = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder.c_str(), nullptr, environmentHandler.Get());
    DesignerLog::Write(
        "PREVIEW create_environment_start hr=" + HResultText(createResult) +
        " user_data_folder=\"" + DesignerLog::ToUtf8(userDataFolder) + "\"");
    if (FAILED(createResult)) {
        g_state.webViewStarting = false;
    }
}

bool CreateIdeCompatibilityScaffold(HWND hostWindow)
{
    // Some installed IDE visual extensions inspect every WM_MDICREATE child
    // and expect the two-level shape used by the IDE home page. These hidden,
    // zero-sized children make that inspection safe. WebView2 itself remains
    // a direct child of the real MDI document window.
    const HWND levelOne = CreateWindowExA(
        0, "Static", "", WS_CHILD, 0, 0, 0, 0,
        hostWindow, nullptr, g_state.module, nullptr);
    if (levelOne == nullptr) {
        return false;
    }
    const HWND levelTwo = CreateWindowExA(
        0, "Static", "", WS_CHILD, 0, 0, 0, 0,
        levelOne, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)),
        g_state.module, nullptr);
    return levelTwo != nullptr;
}

void PaintHostBackground(HWND window)
{
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT rect{};
    GetClientRect(window, &rect);
    HBRUSH brush = CreateSolidBrush(RGB(245, 247, 251));
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(71, 85, 105));
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT)));
    const wchar_t* text = g_state.webViewStarting
        ? L"正在启动 JadeDesigner 预览…"
        : L"WebView2 尚未就绪，请查看 JadeDesigner.log";
    DrawTextW(dc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    EndPaint(window, &paint);
}

LRESULT CALLBACK HostWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (g_state.shuttingDown && message != WM_NCDESTROY) {
        if (message == kRouteUiEventMessage) {
            delete reinterpret_cast<PendingUiEvent*>(lParam);
        }
        return 0;
    }
    switch (message) {
    case WM_CREATE:
        if (!CreateIdeCompatibilityScaffold(window)) {
            DesignerLog::Write(
                "PREVIEW MDI compatibility scaffold failed error=" +
                std::to_string(GetLastError()));
            return -1;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintHostBackground(window);
        return 0;
    case WM_SIZE: {
        const LRESULT result = DefMDIChildProcA(window, message, wParam, lParam);
        ResizeController();
        return result;
    }
    case WM_MDIACTIVATE: {
        const LRESULT result = DefMDIChildProcA(window, message, wParam, lParam);
        const bool active = reinterpret_cast<HWND>(lParam) == window;
        if (g_state.active != active) {
            g_state.active = active;
            DesignerLog::Write(active
                ? "PREVIEW native_mdi_activated"
                : "PREVIEW native_mdi_deactivated");
        }
        PostMessageW(window,kNativeToolboxMessage,0,0);
        if (g_state.controller) {
            g_state.controller->put_IsVisible(active ? TRUE : FALSE);
        }
        if (g_state.compatTabWindow != nullptr) {
            InvalidateRect(g_state.compatTabWindow, nullptr, FALSE);
        }
        if (active) {
            UpdateCodeTabCaption();
            ResizeController();
            EnsureWebView();
        }
        const HWND documentToMaximize = active
            ? window
            : reinterpret_cast<HWND>(lParam);
        if (!g_state.maximizingDocument &&
            documentToMaximize != nullptr && IsWindow(documentToMaximize)) {
            PostMessageA(
                window, kMaximizeDocumentMessage,
                reinterpret_cast<WPARAM>(documentToMaximize), 0);
        }
        return result;
    }
    case kMaximizeDocumentMessage:
        MaximizeMdiDocument(reinterpret_cast<HWND>(wParam));
        return 0;
    case kRouteUiEventMessage:
        ProcessQueuedUiEvent(reinterpret_cast<PendingUiEvent*>(lParam));
        return 0;
    case kNativeToolboxMessage:
        g_nativeToolbox.SetEditable(g_visualDesignMode);
        g_nativeToolbox.Update(WebPreview::IsActive());
        return 0;
    case WM_TIMER:
        if (wParam == kRefreshTimerId) {
            RetryCodeTabCaption();
            UpdateCompatTabLayout();
            WebPreview::Layout();
            PollFileChanges();
            g_nativeToolbox.Update(WebPreview::IsActive());
            return 0;
        }
        break;
    case WM_SETFOCUS:
        if (g_state.controller) {
            g_state.controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }
        return 0;
    case WM_CLOSE:
        // Keep the fixed preview page registered and switch to another IDE
        // document instead of leaving a stale native tab behind.
        WebPreview::Hide();
        return 0;
    case WM_NCDESTROY:
        if (g_state.hostWindow == window) {
            g_state.hostWindow = nullptr;
            g_state.active = false;
        }
        break;
    default:
        break;
    }
    return DefMDIChildProcA(window, message, wParam, lParam);
}

bool RegisterWindowClasses()
{
    if (g_state.classesReady) {
        return true;
    }

    WNDCLASSEXA hostClass{};
    hostClass.cbSize = sizeof(hostClass);
    hostClass.hInstance = g_state.module;
    hostClass.lpfnWndProc = HostWindowProc;
    hostClass.lpszClassName = kHostClassName;
    hostClass.hCursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512));
    hostClass.hbrBackground = nullptr;
    if (RegisterClassExA(&hostClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        DesignerLog::Write("PREVIEW host RegisterClassEx failed error=" + std::to_string(GetLastError()));
        return false;
    }

    WNDCLASSEXW compatTabClass{};
    compatTabClass.cbSize = sizeof(compatTabClass);
    compatTabClass.hInstance = g_state.module;
    compatTabClass.lpfnWndProc = CompatTabWindowProc;
    compatTabClass.lpszClassName = kCompatTabClassName;
    compatTabClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32649));
    compatTabClass.hbrBackground = nullptr;
    if (RegisterClassExW(&compatTabClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        DesignerLog::Write(
            "PREVIEW compatibility tab RegisterClassEx failed error=" +
            std::to_string(GetLastError()));
        UnregisterClassA(kHostClassName, g_state.module);
        return false;
    }

    g_state.classesReady = true;
    return true;
}

} // namespace

namespace WebPreview {

void InitializeModule(HMODULE module)
{
    g_state.module = module;
    g_state.indexPath = BuildIndexPath();
}

bool Attach(HWND mainWindow, HWND mdiClient, HWND codeTab)
{
    if (g_state.hostWindow != nullptr && IsWindow(g_state.hostWindow) &&
        g_state.mainWindow == mainWindow && g_state.mdiClient == mdiClient &&
        g_state.codeTab == codeTab) {
        return true;
    }
    if (g_state.hostWindow != nullptr || g_state.mainWindow != nullptr ||
        g_state.mdiClient != nullptr || g_state.controller ||
        g_state.environment || g_state.webView) {
        Shutdown();
    }
    if (!IsWindow(mainWindow) || !IsWindow(mdiClient) || !IsWindow(codeTab) || !RegisterWindowClasses()) {
        return false;
    }

    g_state.shuttingDown = false;
    ++g_state.generation;
    g_state.mainWindow = mainWindow;
    g_state.mdiClient = mdiClient;
    g_state.codeTab = codeTab;
    g_nativeToolbox.Initialize(mainWindow,g_state.module,[] { IdeEventRouter::ToggleNativeComponentBar(); },
        [](const std::wstring& kind) {
            if(g_state.webView && (kind==L"pointer" || kind==L"unavailable" || (g_visualDesignMode && WebPreview::IsActive())))
                g_state.webView->PostWebMessageAsString((L"JADE_NATIVE_TOOLBOX\t"+kind).c_str());
        });
    g_state.tabCaptionEverUpdated = false;
    g_state.tabCaptionUpdateAttempts = 0;
    CaptureCodeTabInsertionPoint();

    const HWND previousActive = reinterpret_cast<HWND>(
        SendMessageA(mdiClient, WM_MDIGETACTIVE, 0, 0));
    const std::string caption = WideToAnsi(kTabText);
    g_state.hostWindow = CreateMDIWindowA(
        kHostClassName,
        caption.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CHILD | WS_VISIBLE |
            WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_MAXIMIZE,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        mdiClient,
        g_state.module,
        0);
    if (g_state.hostWindow == nullptr) {
        DesignerLog::Write(
            "PREVIEW MDI CreateMDIWindow failed error=" +
            std::to_string(GetLastError()));
        return false;
    }
    EnsureCompatTabWindow();

    // WM_MDICREATE has registered the real document. Update its custom tab
    // item directly so the title has a non-zero width. WM_SETTEXT remains as
    // a fallback for an unthemed/native implementation.
    const bool tabUpdated = UpdateCodeTabCaption();
    ++g_state.tabCaptionUpdateAttempts;
    SetWindowTextA(g_state.hostWindow, caption.c_str());
    if (!tabUpdated) {
        DesignerLog::Write(
            "PREVIEW native_code_tab_update not available; used WM_SETTEXT fallback");
    }
    SetTimer(g_state.hostWindow, kRefreshTimerId, kRefreshIntervalMs, nullptr);
    if (previousActive != nullptr && previousActive != g_state.hostWindow &&
        IsWindow(previousActive)) {
        SendMessageA(
            mdiClient, WM_MDIACTIVATE,
            reinterpret_cast<WPARAM>(previousActive), 0);
        MaximizeMdiDocument(previousActive);
        ShowWindow(g_state.hostWindow, SW_HIDE);
        g_state.active = false;
    }
    else {
        Show();
    }
    DesignerLog::Write(
        "PREVIEW native_mdi_tab_attached main=" + DesignerLog::HexPointer(mainWindow) +
        " mdi=" + DesignerLog::HexPointer(mdiClient) +
        " code_tab=" + DesignerLog::HexPointer(codeTab) +
        " host=" + DesignerLog::HexPointer(g_state.hostWindow) +
        " index_path=\"" + DesignerLog::ToUtf8(g_state.indexPath) + "\"");
    return true;
}

void Toggle()
{
    IsActive() ? Hide() : Show();
}

void Show()
{
    if (g_state.hostWindow == nullptr || g_state.mdiClient == nullptr) {
        DesignerLog::Write("PREVIEW show skipped: integration is not attached");
        return;
    }
    ShowWindow(g_state.hostWindow, SW_SHOW);
    UpdateCodeTabCaption();
    UpdateCompatTabLayout();
    SendMessageA(
        g_state.mdiClient, WM_MDIACTIVATE,
        reinterpret_cast<WPARAM>(g_state.hostWindow), 0);
    MaximizeMdiDocument(g_state.hostWindow);
    g_state.active = true;
    if (g_state.controller) {
        g_state.controller->put_IsVisible(TRUE);
    }
    EnsureWebView();
    DesignerLog::Write("PREVIEW activated through native MDI tab");
}

void Hide()
{
    if (!g_state.active || g_state.mdiClient == nullptr ||
        !IsWindow(g_state.mdiClient) || g_state.hostWindow == nullptr ||
        !IsWindow(g_state.hostWindow)) {
        return;
    }
    HWND nextWindow = GetWindow(g_state.mdiClient, GW_CHILD);
    while (nextWindow != nullptr) {
        if (nextWindow != g_state.hostWindow &&
            (GetWindowLongPtrW(nextWindow, GWL_EXSTYLE) & WS_EX_MDICHILD) != 0) {
            break;
        }
        nextWindow = GetWindow(nextWindow, GW_HWNDNEXT);
    }
    if (nextWindow != nullptr) {
        SendMessageA(
            g_state.mdiClient, WM_MDIACTIVATE,
            reinterpret_cast<WPARAM>(nextWindow), 0);
        ShowWindow(nextWindow, SW_SHOW);
        g_state.active = false;
        if (g_state.controller) {
            g_state.controller->put_IsVisible(FALSE);
        }
    }
    else {
        ShowWindow(g_state.hostWindow, SW_HIDE);
        g_state.active = false;
        if (g_state.controller) {
            g_state.controller->put_IsVisible(FALSE);
        }
    }
    DesignerLog::Write("PREVIEW switched to another native MDI page");
}

void Refresh()
{
    if (!g_state.webView) {
        Show();
        return;
    }
    NavigateConfiguredPage();
    DesignerLog::Write("PREVIEW manual_refresh");
}

void Layout()
{
    UpdateCompatTabLayout();
    if (g_state.hostWindow != nullptr && IsWindow(g_state.hostWindow)) {
        ResizeController();
    }
}

bool IsActive()
{
    if (g_state.mdiClient != nullptr && IsWindow(g_state.mdiClient) &&
        g_state.hostWindow != nullptr && IsWindow(g_state.hostWindow)) {
        const HWND activeWindow = reinterpret_cast<HWND>(
            SendMessageA(g_state.mdiClient, WM_MDIGETACTIVE, 0, 0));
        g_state.active = activeWindow == g_state.hostWindow &&
                         IsWindowVisible(g_state.hostWindow) != FALSE;
    }
    return g_state.active;
}

bool IsAttached()
{
    return g_state.hostWindow != nullptr && IsWindow(g_state.hostWindow) &&
           g_state.mdiClient != nullptr && IsWindow(g_state.mdiClient);
}

void Shutdown()
{
    g_nativeToolbox.Shutdown();
    g_visualDesignMode=false;
    g_state.shuttingDown = true;
    ++g_state.generation;
    g_state.active = false;
    if (g_state.compatTabWindow != nullptr && IsWindow(g_state.compatTabWindow)) {
        DestroyWindow(g_state.compatTabWindow);
    }
    g_state.compatTabWindow = nullptr;
    g_state.compatTabHover = false;
    if (g_state.compatTabFont != nullptr) {
        DeleteObject(g_state.compatTabFont);
        g_state.compatTabFont = nullptr;
    }
    if (g_state.hostWindow != nullptr) {
        KillTimer(g_state.hostWindow, kRefreshTimerId);
    }
    if (g_state.webView && g_state.navigationTokenValid) {
        g_state.webView->remove_NavigationCompleted(g_state.navigationToken);
    }
    g_state.navigationTokenValid = false;
    if(g_state.webView && g_state.navigationStartingTokenValid) {
        g_state.webView->remove_NavigationStarting(g_state.navigationStartingToken);
    }
    g_state.navigationStartingTokenValid=false;
    if (g_state.webView && g_state.webMessageTokenValid) {
        g_state.webView->remove_WebMessageReceived(g_state.webMessageToken);
    }
    g_state.webMessageTokenValid = false;
    if (g_state.controller) {
        g_state.controller->Close();
    }
    g_state.webView.Reset();
    g_state.controller.Reset();
    g_state.environment.Reset();
    const HWND hostWindow = g_state.hostWindow;
    if (hostWindow != nullptr && IsWindow(hostWindow)) {
        if (g_state.mdiClient != nullptr && IsWindow(g_state.mdiClient)) {
            SendMessageA(
                g_state.mdiClient, WM_MDIDESTROY,
                reinterpret_cast<WPARAM>(hostWindow), 0);
        }
        if (IsWindow(hostWindow)) {
            DestroyWindow(hostWindow);
        }
    }
    g_state.hostWindow = nullptr;
    g_state.mainWindow = nullptr;
    g_state.mdiClient = nullptr;
    g_state.codeTab = nullptr;
    g_state.tabMessageTarget = nullptr;
    g_state.webViewStarting = false;
    g_state.currentPageIsFile = false;
    g_state.lastWriteValid = false;
    g_state.maximizingDocument = false;
    g_state.tabCaptionEverUpdated = false;
    g_state.codeTabIndex = -1;
    g_state.tabCaptionUpdateAttempts = 0;
    if (g_state.comNeedsUninitialize) {
        CoUninitialize();
    }
    g_state.comNeedsUninitialize = false;
    g_state.comAttempted = false;
    if (g_state.classesReady) {
        UnregisterClassW(kCompatTabClassName, g_state.module);
        UnregisterClassA(kHostClassName, g_state.module);
        g_state.classesReady = false;
    }
    DesignerLog::Write("PREVIEW shutdown complete");
}

} // namespace WebPreview
