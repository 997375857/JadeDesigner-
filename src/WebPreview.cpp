#include "WebPreview.h"

#include "DesignerLog.h"
#include "IdeEventRouter.h"

#include <CommCtrl.h>
#include <Shlwapi.h>
#include <WebView2.h>
#include <windowsx.h>
#include <wrl.h>

#include <cwchar>
#include <algorithm>
#include <cmath>
#include <iterator>
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
    EventRegistrationToken webMessageToken{};
    bool webMessageTokenValid = false;
    unsigned long long generation = 0;
    bool shuttingDown = false;
};

PreviewState g_state;

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
    constexpr const wchar_t* script = LR"JS(
(() => {
  if (window.__jadeDesignerEventBridgeInstalled) return 'already-installed';
  window.__jadeDesignerEventBridgeInstalled = true;
  const encode = value => encodeURIComponent(String(value ?? ''));
  const readableName = element => {
    const explicitName = element.dataset.jadeName || element.id || element.name ||
      element.getAttribute('aria-label') || element.title;
    if (explicitName) return explicitName;
    const text = (element.textContent || element.value || element.tagName || 'Jade控件').trim();
    return text.slice(0, 60) || 'Jade控件';
  };
  const hasReadableName = element => !!(element.dataset.jadeName || element.id ||
    element.name || element.getAttribute('aria-label') || element.title);
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
  const emit = (domEvent, element, eventName, controlType) => {
    const elementId = readableName(element);
    const captured = (window.__jadeLastChannel || '').trim();
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
    window.chrome.webview.postMessage('JADE_EVT\t' + fields.map(encode).join('\t'));
  };
  const scheduleEmit = (domEvent, element, eventName, controlType) => {
    if (!element) return;
    window.__jadeLastChannel = '';
    setTimeout(() => {
      const stillThere = element.isConnected || element === document.activeElement;
      if (stillThere) emit(domEvent, element, eventName, controlType);
    }, 0);
  };
  document.addEventListener('click', event => {
    const source = event.target instanceof Element ? event.target : event.target?.parentElement;
    const element = source?.closest('button,[role="button"],input[type="button"],input[type="submit"]');
    if (element) scheduleEmit('click', element, '被单击', 'button');
  }, true);
  document.addEventListener('change', event => {
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
  window.chrome.webview.addEventListener('message', event => {
    if (typeof event.data !== 'string') return;
    const status = document.querySelector('[data-jade-status]') || document.getElementById('eventLog');
    if (status) status.textContent = event.data;
  });
  return 'installed';
})()
)JS";
    const HRESULT result = g_state.webView->ExecuteScript(script, nullptr);
    DesignerLog::Write("PREVIEW install_ui_event_bridge hr=" + HResultText(result));
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
    IdeEventRouter::UiEvent event;
    if (!IdeEventRouter::TryParseWebMessage(wireMessage, event)) {
        DesignerLog::Write("UI_EVENT ignored invalid_wire_message");
        return S_OK;
    }

    const IdeEventRouter::RouteResult result = IdeEventRouter::Route(
        g_state.mainWindow, g_state.mdiClient, event);
    DesignerLog::Write(
        "UI_EVENT result success=" + std::to_string(result.succeeded ? 1 : 0) +
        " action=" + result.action + " message=\"" + result.message + "\"");
    if (g_state.webView) {
        const std::wstring acknowledgement = IdeEventRouter::BuildAckMessage(result);
        g_state.webView->PostWebMessageAsString(acknowledgement.c_str());
    }
    return S_OK;
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
    DesignerLog::Write("PREVIEW ui_event_bridge_build=5 native_menu_page_edit=1");

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
    case WM_TIMER:
        if (wParam == kRefreshTimerId) {
            RetryCodeTabCaption();
            UpdateCompatTabLayout();
            WebPreview::Layout();
            PollFileChanges();
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
