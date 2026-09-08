#include "IdeEventRouter.h"

#include "DesignerLog.h"
#include "HookBridge.h"
#include "ProjectAssembly.h"
#include "PageSource.h"
#include <PublicIDEFunctions.h>
#include <fnshare.h>
#include <lib2.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using PageSource::PageCodeInfo;
using PageSource::ParsePageCode;
using PageSource::LeadingIdentifier;

constexpr wchar_t kDefaultAssembly[] = L"Jade通讯注册事件";
constexpr wchar_t kSharedAssembly[] = L"UI_JadeView";
constexpr wchar_t kSubscribeAssembly[] = L"Jade_通讯_订阅集";
constexpr wchar_t kSubscribeSub[] = L"Jade_通讯_订阅";
constexpr wchar_t kRegisterAssembly[] = L"UI_启动JadeView";
constexpr wchar_t kRegisterSub[] = L"UI_启动JadeView";
constexpr int kMaximumRowsToScan = 4096;
constexpr int kMaximumColumnsToScan = 4;
constexpr int kMaximumMissingRowsToStop = 64;
// Gap tolerance for the rescan used before declaring a subroutine absent.
// Wider than the everyday heuristic, still bounded: every row costs four IDE
// round trips.
constexpr int kThoroughMissingRowsToStop = 192;

// Easy Language 5.95 native menu command identifier, verified from the
// target e5.95.exe menu resources (SHA256 368CBBD3...ABE1409).
constexpr UINT kMenuInsertAssembly = 32782; // 0x800E -> FN_INSERT_NEW_MOD
constexpr DWORD kNativeCommandSettleMs = 35;
constexpr DWORD kInsertSettleMs = 250;
// e5.95 stores and parses source text as Simplified-Chinese GBK, independent
// of the Windows process ACP. Keep all bytes crossing the IDE boundary on
// the explicit code page so UTF-8 is never pasted as mojibake.
constexpr UINT kIdeCodePage = 936;

// Source text uses the native model bridge. Individual navigation and property
// operations use NotifySys/NES_RUN_FUNC; source reads have no alternate path.
std::atomic_bool g_routingInProgress{false};
std::set<std::string> g_writtenSubscriptionKeys;
// Repeated clicks still verify source presence, but avoid the repair path when
// the current memory snapshot proves both callback and subscription are intact.
std::set<std::string> g_fastJumpKeys;
struct RoutingGuard {
    ~RoutingGuard() { g_routingInProgress = false; }
};

template <typename T>
DWORD PointerToDword(T* value)
{
    return static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(value));
}

bool InvokeIde(INT functionCode, DWORD parameter1 = 0, DWORD parameter2 = 0)
{
    DWORD parameters[2] = {parameter1, parameter2};
    return NotifySys(NES_RUN_FUNC, functionCode, PointerToDword(parameters)) != FALSE;
}

std::wstring Utf8ToWide(std::string_view value)
{
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required);
    return result;
}

std::wstring AnsiToWide(std::string_view value)
{
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        kIdeCodePage, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(
        kIdeCodePage, 0, value.data(), static_cast<int>(value.size()),
        result.data(), required);
    return result;
}

std::string WideToUtf8(std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), required, nullptr, nullptr);
    return result;
}

std::string WideToAnsi(std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        kIdeCodePage, WC_NO_BEST_FIT_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(required), '\0');
    BOOL usedDefault = FALSE;
    WideCharToMultiByte(
        kIdeCodePage, WC_NO_BEST_FIT_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), required, nullptr, &usedDefault);
    if (usedDefault != FALSE) {
        return {};
    }
    return result;
}

int HexValue(wchar_t value)
{
    if (value >= L'0' && value <= L'9') {
        return value - L'0';
    }
    value = static_cast<wchar_t>(towlower(value));
    if (value >= L'a' && value <= L'f') {
        return value - L'a' + 10;
    }
    return -1;
}

std::string PercentDecodeUtf8(std::wstring_view value)
{
    std::string bytes;
    bytes.reserve(value.size());
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] == L'%' && index + 2 < value.size()) {
            const int high = HexValue(value[index + 1]);
            const int low = HexValue(value[index + 2]);
            if (high >= 0 && low >= 0) {
                bytes.push_back(static_cast<char>((high << 4) | low));
                index += 2;
                continue;
            }
        }
        if (value[index] <= 0x7F) {
            bytes.push_back(static_cast<char>(value[index]));
        }
        else {
            bytes += WideToUtf8(std::wstring_view(&value[index], 1));
        }
    }
    return bytes;
}

std::vector<std::wstring_view> SplitTabs(std::wstring_view value)
{
    std::vector<std::wstring_view> result;
    size_t begin = 0;
    while (begin <= value.size()) {
        const size_t end = value.find(L'\t', begin);
        result.push_back(value.substr(
            begin, end == std::wstring_view::npos ? value.size() - begin : end - begin));
        if (end == std::wstring_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

std::wstring SanitizeIdentifier(std::wstring value, std::wstring_view fallback)
{
    // E-language names accept letters, digits, underscore and full-width
    // (non-ASCII) characters; everything else - notably the '-' in web
    // element ids like "btn-import-accounts" - is silently rejected by the
    // IDE's name validator, so replace all ASCII non-word characters.
    for (wchar_t& character : value) {
        const bool asciiWord =
            (character >= L'0' && character <= L'9') ||
            (character >= L'a' && character <= L'z') ||
            (character >= L'A' && character <= L'Z') ||
            character == L'_';
        if (!asciiWord && character < 0x80) {
            character = L'_';
        }
    }
    while (!value.empty() && value.front() == L'_') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == L'_') {
        value.pop_back();
    }
    if (!value.empty() && value.front() >= L'0' && value.front() <= L'9') {
        value.insert(value.begin(), L'_');
    }
    if (value.empty()) {
        value.assign(fallback);
    }
    if (value.size() > 120) {
        value.resize(120);
    }
    return value;
}

bool StartsWithInsensitive(std::wstring_view value, std::wstring_view prefix)
{
    if (value.size() < prefix.size()) {
        return false;
    }
    return _wcsnicmp(value.data(), prefix.data(), prefix.size()) == 0;
}

bool ContainsInsensitive(std::wstring_view value, std::wstring_view needle)
{
    if (needle.empty()) {
        return true;
    }
    if (value.size() < needle.size()) {
        return false;
    }
    for (size_t index = 0; index + needle.size() <= value.size(); ++index) {
        if (_wcsnicmp(value.data() + index, needle.data(), needle.size()) == 0) {
            return true;
        }
    }
    return false;
}

bool IsCommonInteractiveControl(const IdeEventRouter::UiEvent& event)
{
    return _stricmp(event.controlType.c_str(), "button") == 0 ||
        _stricmp(event.controlType.c_str(), "select") == 0 ||
        _stricmp(event.controlType.c_str(), "radio") == 0 ||
        _stricmp(event.controlType.c_str(), "checkbox") == 0;
}

std::wstring DefaultHandlerSuffix(const IdeEventRouter::UiEvent& event)
{
    if (_stricmp(event.controlType.c_str(), "select") == 0) {
        return L"_选择项被改变";
    }
    if (_stricmp(event.controlType.c_str(), "radio") == 0 ||
        _stricmp(event.controlType.c_str(), "checkbox") == 0) {
        return L"_选中状态被改变";
    }
    return L"_被单击";
}

bool IsWindowControlEvent(const IdeEventRouter::UiEvent& event)
{
    if (_stricmp(event.controlType.c_str(), "button") != 0) {
        return false;
    }
    const std::wstring identity =
        Utf8ToWide(event.elementId) + L" " + Utf8ToWide(event.handlerName) + L" " +
        Utf8ToWide(event.callParam);
    const bool hasWindowContext =
        ContainsInsensitive(identity, L"window") ||
        ContainsInsensitive(identity, L"modal") ||
        ContainsInsensitive(identity, L"dialog") ||
        ContainsInsensitive(identity, L"browser") ||
        ContainsInsensitive(identity, L"qr");
    const bool hasWindowAction =
        ContainsInsensitive(identity, L"close") ||
        ContainsInsensitive(identity, L"hide") ||
        ContainsInsensitive(identity, L"dismiss") ||
        ContainsInsensitive(identity, L"minimize") ||
        ContainsInsensitive(identity, L"maximize");
    const std::wstring channel = Utf8ToWide(event.callParam);
    return StartsWithInsensitive(channel, L"win:") ||
        (hasWindowContext && hasWindowAction);
}
// Derives the callback subroutine name for a channel subscription. Known
// channels map to the established Chinese naming (ipc_窗口最小化); anything
// else falls back to ipc_ + sanitized tail of the channel.
std::wstring DeriveIpcNameFromChannel(std::wstring_view channel)
{
    struct ChannelMapping {
        std::wstring_view channel;
        std::wstring_view handler;
    };
    static constexpr ChannelMapping kMappings[] = {
        {L"win:minimize", L"ipc_窗口最小化"},
        {L"win:close", L"ipc_窗口关闭"},
        {L"win:maximize", L"ipc_窗口最大化"},
        {L"win:restore", L"ipc_窗口还原"},
        {L"win:hide", L"ipc_窗口隐藏"},
        {L"win:show", L"ipc_窗口显示"},
        {L"win:fullscreen", L"ipc_窗口全屏"},
        {L"app:login_result", L"ipc_登录结果"},
        {L"app:login_timeout", L"ipc_登录超时"},
        {L"app:login_success", L"ipc_登录成功"},
        {L"event-log", L"ipc_事件日志"},
        {L"app-toast", L"ipc_提示消息"},
        {L"qr-image", L"ipc_二维码图片"},
        {L"qr-login-success", L"ipc_二维码登录成功"},
        {L"qr-login-timeout", L"ipc_二维码登录超时"},
        {L"account-list", L"ipc_账号列表"},
        {L"group-list", L"ipc_群组列表"},
        {L"forward-count", L"ipc_转发计数"},
        {L"forward-log", L"ipc_转发日志"},
    };
    for (const ChannelMapping& mapping : kMappings) {
        if (_wcsicmp(channel.data(), mapping.channel.data()) == 0) {
            return std::wstring(mapping.handler);
        }
    }
    const size_t colon = channel.find(L':');
    const std::wstring_view tail =
        colon == std::wstring_view::npos ? channel : channel.substr(colon + 1);
    std::wstring handler = L"ipc_";
    for (wchar_t character : tail) {
        const bool asciiWord =
            (character >= L'0' && character <= L'9') ||
            (character >= L'a' && character <= L'z') ||
            (character >= L'A' && character <= L'Z');
        handler.push_back(asciiWord ? character : L'_');
    }
    return handler;
}

void PumpMessagesFor(DWORD durationMs)
{
    const DWORD start = GetTickCount();
    MSG message{};
    while (GetTickCount() - start < durationMs) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(5);
    }
}

void SettleNativeCommand(HWND mainWindow)
{
    if (mainWindow != nullptr && IsWindow(mainWindow)) {
        UpdateWindow(mainWindow);
    }
    PumpMessagesFor(kNativeCommandSettleMs);
}

void RunNativeMenuCommand(HWND mainWindow, UINT commandId, const char* label)
{
    DesignerLog::Write(
        "UI_EVENT native_menu begin label=" + std::string(label != nullptr ? label : "unknown") +
        " id=" + std::to_string(commandId));
    SendMessageW(
        mainWindow,
        WM_COMMAND,
        MAKEWPARAM(commandId, 0),
        0);
    SettleNativeCommand(mainWindow);
    DesignerLog::Write(
        "UI_EVENT native_menu end label=" + std::string(label != nullptr ? label : "unknown") +
        " id=" + std::to_string(commandId));
}

std::vector<HWND> SnapshotMdiChildren(HWND mdiClient)
{
    std::vector<HWND> result;
    HWND child = GetWindow(mdiClient, GW_CHILD);
    while (child != nullptr) {
        if (GetParent(child) == mdiClient &&
            (GetWindowLongPtrW(child, GWL_EXSTYLE) & WS_EX_MDICHILD) != 0) {
            result.push_back(child);
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
    return result;
}

// Detects the newly inserted assembly page by diffing the MDI child list.
// WM_MDIGETACTIVE-based detection proved unreliable here: the IDE visual
// layer can keep the client's active-child state on the previous document.
HWND WaitForNewMdiDocument(HWND mdiClient, const std::vector<HWND>& knownChildren)
{
    for (int attempt = 0; attempt < 50; ++attempt) {
        for (HWND child : SnapshotMdiChildren(mdiClient)) {
            if (std::find(knownChildren.begin(), knownChildren.end(), child) ==
                knownChildren.end()) {
                return child;
            }
        }
        Sleep(20);
    }
    return nullptr;
}

std::wstring GetWindowTitle(HWND window)
{
    const int length = GetWindowTextLengthW(window);
    if (length <= 0 || length > 4096) {
        return {};
    }
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    title.resize(copied > 0 ? static_cast<size_t>(copied) : 0);
    return title;
}

HWND FindMdiDocument(HWND mdiClient, std::wstring_view pageName)
{
    return ProjectAssembly::FindOpenDocument(mdiClient, pageName);
}

ProjectAssembly::Lookup LookupAssemblyPage(
    HWND mainWindow, HWND mdiClient, std::wstring_view name)
{
    auto found = ProjectAssembly::FindAndOpen(mainWindow, mdiClient, name);
    DesignerLog::Write(
        "UI_EVENT assembly_lookup name=\"" + WideToUtf8(name) +
        "\" source=program_tree state=" +
        (found.state == ProjectAssembly::State::Found ? "found" :
         found.state == ProjectAssembly::State::Absent ? "absent" :
         found.state == ProjectAssembly::State::Ambiguous ? "ambiguous" : "unavailable") +
        " matches=" + std::to_string(found.matches) +
        " nodes=" + std::to_string(found.nodes) +
        " document=" + DesignerLog::HexPointer(found.document) +
        " reason=" + found.reason);
    return found;
}

std::string AssemblyLookupFailure(
    const ProjectAssembly::Lookup& found, std::wstring_view name)
{
    if (found.state == ProjectAssembly::State::Ambiguous) {
        return "程序集名称冲突：" + WideToUtf8(name) +
            "，项目中有多份同名项目，已停止写入，请先检查程序工作夹";
    }
    if (found.state == ProjectAssembly::State::Found) {
        return "程序集 " + WideToUtf8(name) +
            " 已存在，但其代码页无法打开，已停止创建，请在程序工作夹中打开后重试";
    }
    return "无法完整读取程序工作夹，不能确认程序集 " + WideToUtf8(name) +
        " 是否存在，已停止创建（" + found.reason + "）";
}

// The IDE holds one project at a time and carries its path in the main window
// title, ahead of the "[程序集名]" suffix that changes with the active document.
bool ExtractProjectPath(const std::wstring& title, std::wstring& projectPath)
{
    for (size_t index = 0; index + 3 < title.size(); ++index) {
        const wchar_t drive = title[index];
        const bool isDriveLetter =
            (drive >= L'A' && drive <= L'Z') || (drive >= L'a' && drive <= L'z');
        if (!isDriveLetter || title[index + 1] != L':' || title[index + 2] != L'\\') {
            continue;
        }
        for (size_t end = index + 3; end + 1 < title.size(); ++end) {
            if (title[end] == L'.' &&
                (title[end + 1] == L'e' || title[end + 1] == L'E') &&
                (end + 2 == title.size() || title[end + 2] == L' ' ||
                 title[end + 2] == L'[')) {
                projectPath = title.substr(index, end + 2 - index);
                return true;
            }
        }
    }
    return false;
}

// Every cache below describes one project's pages. Opening another project
// closes all of them, so carrying the entries over would let this project be
// judged by what the previous one contained: a remembered subscription would
// suppress a write this project still needs, and a remembered assembly would
// refuse to create one it does not have yet.
void ResetSessionCachesIfProjectChanged(HWND mainWindow)
{
    if (mainWindow == nullptr || !IsWindow(mainWindow)) {
        return;
    }
    wchar_t title[1024]{};
    GetWindowTextW(mainWindow, title, 1024);
    std::wstring projectPath;
    // An unsaved new project has no path in the title. Keeping the caches is
    // the safer half of that guess: they still describe pages that are open.
    if (!ExtractProjectPath(title, projectPath)) {
        return;
    }
    // Also covers the case where the preview window is closed: routing still
    // runs, so the log still follows the project rather than going quiet.
    DesignerLog::UseProjectFile(projectPath);
    static std::wstring currentProject;
    if (_wcsicmp(currentProject.c_str(), projectPath.c_str()) == 0) {
        return;
    }
    const bool hadPreviousProject = !currentProject.empty();
    currentProject = projectPath;
    if (!hadPreviousProject) {
        return;
    }
    g_fastJumpKeys.clear();
    g_writtenSubscriptionKeys.clear();
    DesignerLog::Write(
        "UI_EVENT session_cache_reset project=\"" + WideToUtf8(projectPath) + "\"");
}

HWND FindAnyNativeMdiDocument(HWND mdiClient)
{
    HWND child = GetWindow(mdiClient, GW_CHILD);
    while (child != nullptr) {
        if (GetParent(child) == mdiClient &&
            (GetWindowLongPtrW(child, GWL_EXSTYLE) & WS_EX_MDICHILD) != 0) {
            const std::wstring title = GetWindowTitle(child);
            if (_wcsicmp(title.c_str(), L"Jade预览") != 0) {
                return child;
            }
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
    return nullptr;
}

bool ActivateDocument(HWND mdiClient, HWND document)
{
    if (document == nullptr || !IsWindow(document)) {
        return false;
    }
    ShowWindow(document, SW_SHOW);
    SendMessageW(mdiClient, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(document), 0);
    SetFocus(document);
    return reinterpret_cast<HWND>(SendMessageW(mdiClient, WM_MDIGETACTIVE, 0, 0)) == document;
}

struct CellText {
    int type = 0;
    bool isTitle = false;
    int reportedSize = 0; // m_nBufSize from the size query (text length + 1)
    std::string text;
};

bool ReadCell(int row, int column, CellText& result)
{
    result = {};
    GET_PRG_TEXT_PARAM query{};
    query.m_nRowIndex = row;
    query.m_nColIndex = column;
    if (!InvokeIde(FN_GET_PRG_TEXT, PointerToDword(&query), 0)) {
        return false;
    }
    result.type = query.m_nType;
    result.isTitle = query.m_blIsTitle != FALSE;
    result.reportedSize = query.m_nBufSize;
    if (query.m_nBufSize <= 0 || query.m_nBufSize > 1024 * 1024) {
        return true;
    }
    std::vector<char> buffer(static_cast<size_t>(query.m_nBufSize) + 1, '\0');
    query.m_pBuf = buffer.data();
    query.m_nBufSize = static_cast<int>(buffer.size());
    if (!InvokeIde(FN_GET_PRG_TEXT, PointerToDword(&query), 0)) {
        return false;
    }
    result.type = query.m_nType;
    result.isTitle = query.m_blIsTitle != FALSE;
    result.text.assign(buffer.data());
    return true;
}

// The handler always returns TRUE - even for out-of-range rows - leaving
// type=0 and reportedSize=1. Real cells carry a nonzero type or content, so
// row detection must require actual cell evidence.
bool CellHasData(const CellText& cell)
{
    return cell.type != 0 || cell.isTitle || cell.reportedSize > 1 ||
        !cell.text.empty();
}

struct PageScanResult {
    int matchRow = -1;
    int matchColumn = -1;
    std::string matchText;
    int typeCellCount = 0;
    int nonEmptyTextCells = 0;
    int lastRow = -1;
};

// Scans the active page. When matchAnyText is true the first NON-EMPTY cell
// of the desired type wins (label/header cells of the same type read back
// empty and must not shadow the value column); otherwise the cell text must
// equal desiredNameAnsi. Non-empty matches always beat empty fallbacks.
PageScanResult ScanPageForType(
    int desiredType,
    const std::string& desiredNameAnsi,
    bool matchAnyText)
{
    PageScanResult result;
    int fallbackRow = -1;
    int fallbackColumn = -1;
    int consecutiveMissingRows = 0;
    for (int currentRow = 0; currentRow < kMaximumRowsToScan; ++currentRow) {
        bool rowExists = false;
        for (int currentColumn = 0; currentColumn < kMaximumColumnsToScan; ++currentColumn) {
            CellText cell;
            if (!ReadCell(currentRow, currentColumn, cell) || !CellHasData(cell)) {
                continue;
            }
            rowExists = true;
            result.lastRow = currentRow;
            if (cell.type != desiredType) {
                continue;
            }
            ++result.typeCellCount;
            if (!cell.text.empty()) {
                ++result.nonEmptyTextCells;
                if (matchAnyText) {
                    if (result.matchRow < 0 || result.matchText.empty()) {
                        result.matchRow = currentRow;
                        result.matchColumn = currentColumn;
                        result.matchText = cell.text;
                    }
                }
                else if (_stricmp(cell.text.c_str(), desiredNameAnsi.c_str()) == 0) {
                    result.matchRow = currentRow;
                    result.matchColumn = currentColumn;
                    result.matchText = cell.text;
                    return result;
                }
            }
            else if (fallbackRow < 0) {
                fallbackRow = currentRow;
                fallbackColumn = currentColumn;
            }
        }
        consecutiveMissingRows = rowExists ? 0 : consecutiveMissingRows + 1;
        if (consecutiveMissingRows >= kMaximumMissingRowsToStop) {
            break;
        }
    }
    if (result.matchRow < 0 && matchAnyText) {
        result.matchRow = fallbackRow;
        result.matchColumn = fallbackColumn;
    }
    return result;
}

int CountCellsOfType(int desiredType)
{
    int count = 0;
    int consecutiveMissingRows = 0;
    for (int currentRow = 0; currentRow < kMaximumRowsToScan; ++currentRow) {
        bool rowExists = false;
        for (int currentColumn = 0; currentColumn < kMaximumColumnsToScan; ++currentColumn) {
            CellText cell;
            if (!ReadCell(currentRow, currentColumn, cell) || !CellHasData(cell)) {
                continue;
            }
            rowExists = true;
            if (cell.type == desiredType) {
                ++count;
            }
        }
        consecutiveMissingRows = rowExists ? 0 : consecutiveMissingRows + 1;
        if (consecutiveMissingRows >= kMaximumMissingRowsToStop) {
            break;
        }
    }
    return count;
}

void DumpPageHead(int rows)
{
    std::string dump;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < kMaximumColumnsToScan; ++column) {
            CellText cell;
            if (!ReadCell(row, column, cell) || !CellHasData(cell)) {
                continue;
            }
            dump += " r" + std::to_string(row) + "c" + std::to_string(column) +
                "{t=" + std::to_string(cell.type) +
                ",ti=" + std::to_string(cell.isTitle ? 1 : 0) +
                ",n=" + std::to_string(cell.reportedSize) +
                ",x=\"" + WideToUtf8(AnsiToWide(cell.text)) + "\"}";
        }
    }
    DesignerLog::Write("UI_EVENT page_dump" + dump);
}

// Creates a subroutine at the bottom of the active page.
//
// FN_INSERT_NEW_SUB creates the valid IDE template, whose name cell is then
// renamed via FN_SET_AND_COMPILE_PRG_ITEM_TEXT - the same commit path the
// IDE's own name-cell editor uses. Do not mix this with textual
// FN_INSERT_TEXT insertion: e5.95 updates the code-grid index lazily, so a
// successful textual insert can look absent briefly and cause a duplicate
// native subroutine if a fallback is attempted.
bool QueryCaret(int& row, int& column)
{
    row = -1;
    column = -1;
    if (!InvokeIde(FN_GET_CARET_ROW_INDEX, PointerToDword(&row), 0)) {
        return false;
    }
    if (!InvokeIde(FN_GET_CARET_COL_INDEX, PointerToDword(&column), 0)) {
        return false;
    }
    return row >= 0 && column >= 0;
}

bool RenameCellAt(int row, int column, const std::string& newNameAnsi)
{
    if (!InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(row), static_cast<DWORD>(column))) {
        return false;
    }
    int caretRow = -1;
    int caretColumn = -1;
    QueryCaret(caretRow, caretColumn);
    if (caretRow >= 0 && (caretRow != row || caretColumn != column)) {
        DesignerLog::Write(
            "UI_EVENT rename_cell caret_mismatch want=" + std::to_string(row) + "/" +
            std::to_string(column) + " got=" + std::to_string(caretRow) + "/" +
            std::to_string(caretColumn));
    }
    PumpMessagesFor(80);
    char nameBuffer[256]{};
    const size_t nameLength = std::min(newNameAnsi.size(), sizeof(nameBuffer) - 1);
    if (nameLength > 0) {
        std::memcpy(nameBuffer, newNameAnsi.data(), nameLength);
    }
    const BOOL setOk = InvokeIde(
        FN_SET_AND_COMPILE_PRG_ITEM_TEXT,
        PointerToDword(nameBuffer),
        FALSE) != FALSE;
    PumpMessagesFor(200);
    CellText verify{};
    const bool readBack = ReadCell(row, column, verify);
    const bool verified = readBack &&
        _stricmp(verify.text.c_str(), newNameAnsi.c_str()) == 0;
    DesignerLog::Write(
        "UI_EVENT rename_cell set=" + std::to_string(setOk ? 1 : 0) +
        " verify=" + std::to_string(verified ? 1 : 0) +
        " row=" + std::to_string(row) +
        " col=" + std::to_string(column) +
        " read_back=\"" + WideToUtf8(AnsiToWide(verify.text)) + "\"");
    return setOk && verified;
}

// ---------- whole-page source text ----------
std::string CompactStatement(std::string_view text);

bool ReadPageCodeUtf8(std::string& pageText)
{
    pageText.clear();
    std::string error;
    if (HookBridge::ReadPageCode(pageText, error) && !pageText.empty()) {
        DesignerLog::Write("HYBRID memory_page_read transport=memory_model bytes=" +
            std::to_string(pageText.size()));
        return true;
    }
    pageText.clear();
    DesignerLog::Write("HYBRID memory_page_read failed error=\"" + error +
        "\" action=stop_without_fallback");
    return false;
}


int CountSubInPage(const PageCodeInfo& info, const std::string& subUtf8)
{
    int count = 0;
    for (const std::string& name : info.subNamesUtf8) {
        if (_stricmp(name.c_str(), subUtf8.c_str()) == 0) {
            ++count;
        }
    }
    return count;
}

// The line the subroutine's header sits on, or -1. Only the first match is
// reported: a page with two copies of a name is a state the caller has to see
// as such, not one to silently pick a side in.
int SubLineFromPageText(const PageCodeInfo& info, const std::string& subUtf8)
{
    for (size_t index = 0; index < info.subNamesUtf8.size(); ++index) {
        if (_stricmp(info.subNamesUtf8[index].c_str(), subUtf8.c_str()) != 0) {
            continue;
        }
        return index < info.subLinesFromText.size()
            ? info.subLinesFromText[index]
            : -1;
    }
    return -1;
}

// Use the IDE's program-item identity before consulting approximate text rows.
// A write still requires a matching name cell at the resulting physical row.
int ResolveRowFromProgramTree(const std::string& subNameAnsi)
{
    const HWND mainWindow = reinterpret_cast<HWND>(NotifySys(NES_GET_MAIN_HWND, 0, 0));
    const HWND mdiClient = FindWindowExW(mainWindow, nullptr, L"MDIClient", nullptr);
    const HWND document = reinterpret_cast<HWND>(SendMessageW(mdiClient, WM_MDIGETACTIVE, 0, 0));
    wchar_t title[2048]{};
    const int copied = GetWindowTextW(document, title, 2048);
    constexpr std::wstring_view prefix = L"程序集: ";
    const std::wstring_view titleView(title);
    if (copied <= 0 || copied >= 2047 || !titleView.starts_with(prefix)) return -1;
    if (!ProjectAssembly::JumpToSubroutine(mainWindow, mdiClient,
            titleView.substr(prefix.size()), AnsiToWide(subNameAnsi))) return -1;
    int row = -1;
    int column = -1;
    QueryCaret(row, column);
    // Native Jump can place the caret in the first parameter instead of the
    // name cell. Probe that small neighbourhood without moving it again.
    const int first = row > 4 ? row - 4 : 0;
    for (int probe = first; row >= 0 && probe <= row + 1; ++probe) {
        for (int col = 0; col < kMaximumColumnsToScan; ++col) {
            CellText cell;
            if (ReadCell(probe, col, cell) && cell.type == VT_SUB_NAME &&
                _stricmp(cell.text.c_str(), subNameAnsi.c_str()) == 0) {
                DesignerLog::Write("UI_EVENT sub_row source=program_tree sub=\"" +
                    WideToUtf8(AnsiToWide(subNameAnsi)) + "\" row=" +
                    std::to_string(probe) + " caret=" + std::to_string(row));
                return probe;
            }
        }
    }
    DesignerLog::Write("UI_EVENT sub_row source=program_tree sub=\"" +
        WideToUtf8(AnsiToWide(subNameAnsi)) + "\" row=-1 caret=" + std::to_string(row));
    return -1;
}

// How far either side of the text hint the grid is probed. The two coordinate
// spaces are close but not identical: a six-callback page put a header on text
// line 48 that the grid held at row 46, so the text line is an aim point rather
// than an address. The near reach covers that gap plus the subscription
// statement each click appends above the target. The far reach is for the second
// pass, which has a fully materialised page to scan and no reason to assume the
// gap stays this small once more subroutines sit above the target.
constexpr int kTextHintReach = 16;
constexpr int kTextHintFarReach = 64;

// Turns a page-text line index into a physical grid row.
//
// The grid answers only for rows it has materialised, which is roughly the
// window around the caret: a subroutine far from wherever the caret was last
// left reads back as absent no matter how long you wait. That is why locating
// one used to mean dragging a 24-row window across the entire document and
// bouncing the caret off both ends - seconds of visible caret movement on a
// page whose text had already been read correctly. The caret does reach any
// row directly, so one move to the line the text reported is enough to
// materialise the target's neighbourhood, after which the exact row can be
// read out of the grid. Returns -1 when the name is not there, which leaves
// the caller's slower path intact rather than jumping somewhere approximate.
int ResolveRowNearTextLine(const std::string& subNameAnsi, int textLine)
{
    const int nativeRow = ResolveRowFromProgramTree(subNameAnsi);
    if (nativeRow >= 0) return nativeRow;
    if (textLine < 0) {
        return -1;
    }
    int landedRow = -1;
    int landedColumn = -1;
    int bottomRow = -1;
    // Two passes, because FN_MOVE_CARET turned out not to be the universal way
    // to materialise a row that it was taken for. It is clamped to the rows the
    // grid already holds, so aiming it past that extent - precisely the case
    // this probe exists for - leaves the caret short of the target and the whole
    // band reading empty. FN_MOVE_BOTTOM is not clamped: it goes to the
    // document's real end and renders the tail on the way. That single call is
    // the only thing the windowed walk did that this did not, and it is why the
    // walk kept locating rows the probe had just reported absent.
    for (int pass = 1; pass <= 2; ++pass) {
        int aimRow = textLine;
        if (pass == 2) {
            InvokeIde(FN_MOVE_BOTTOM, 0, 0);
            PumpMessagesFor(60);
            int bottomColumn = -1;
            QueryCaret(bottomRow, bottomColumn);
            if (bottomRow >= 0 && aimRow > bottomRow) {
                aimRow = bottomRow;
            }
        }
        InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(aimRow), 0);
        PumpMessagesFor(pass == 1 ? 40 : 80);
        QueryCaret(landedRow, landedColumn);
        const int reach = pass == 1 ? kTextHintReach : kTextHintFarReach;
        // Text and grid rows can drift by over 100 lines on a large page.
        // At the tail use the actual clamped aim, not a band beyond EOF.
        const int first = aimRow > reach ? aimRow - reach : 0;
        int last = aimRow + reach;
        if (bottomRow >= 0 && last > bottomRow) {
            last = bottomRow;
        }
        for (int row = first; row <= last && row < kMaximumRowsToScan; ++row) {
            for (int column = 0; column < kMaximumColumnsToScan; ++column) {
                CellText cell;
                if (!ReadCell(row, column, cell) || !CellHasData(cell)) {
                    continue;
                }
                if (cell.type != VT_SUB_NAME || cell.text.empty()) {
                    continue;
                }
                if (_stricmp(cell.text.c_str(), subNameAnsi.c_str()) == 0) {
                    DesignerLog::Write(
                        "HYBRID text_row_hint sub=\"" +
                        WideToUtf8(AnsiToWide(subNameAnsi)) +
                        "\" text_line=" + std::to_string(textLine) +
                        " grid_row=" + std::to_string(row) +
                        " delta=" + std::to_string(row - textLine) +
                        " pass=" + std::to_string(pass) +
                        " caret_landed=" + std::to_string(landedRow));
                    return row;
                }
            }
        }
    }
    // caret_landed short of text_line says the clamp above was in play;
    // caret_landed equal to it says the name is further from its text line than
    // the far reach, which would mean the two coordinate spaces have drifted
    // much further apart than any page has shown so far.
    DesignerLog::Write(
        "HYBRID text_row_hint sub=\"" + WideToUtf8(AnsiToWide(subNameAnsi)) +
        "\" text_line=" + std::to_string(textLine) + " grid_row=-1" +
        " caret_landed=" + std::to_string(landedRow) +
        " bottom_row=" + std::to_string(bottomRow));
    return -1;
}

// Convenience for the callers that hold page text rather than a parsed page:
// resolves a row only when the text proves the name appears exactly once.
int ResolveRowFromPageText(
    const std::string& pageTextUtf8,
    const std::string& subNameAnsi,
    const std::string& subNameUtf8)
{
    if (pageTextUtf8.empty()) {
        return -1;
    }
    const PageCodeInfo page = ParsePageCode(pageTextUtf8);
    if (!page.valid || CountSubInPage(page, subNameUtf8) != 1) {
        return -1;
    }
    return ResolveRowNearTextLine(
        subNameAnsi, SubLineFromPageText(page, subNameUtf8));
}

// Collects the statement lines belonging to subUtf8 - from its .子程序 header
// up to the next one - with whitespace stripped, so tokens match the same way
// CompactStatement makes them match against grid text. The page text is the
// only view of the page that does not stop at the fold, which makes it the only
// source that can answer "is this body line missing" for a subroutine sitting
// below it.
bool SubBlockCompactFromPageText(
    const std::string& pageTextUtf8,
    const std::string& subUtf8,
    std::string& blockCompactUtf8)
{
    blockCompactUtf8.clear();
    const std::string subTag = WideToUtf8(L".子程序");
    bool inBlock = false;
    bool found = false;
    size_t lineStart = 0;
    while (lineStart < pageTextUtf8.size()) {
        size_t lineEnd = pageTextUtf8.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = pageTextUtf8.size();
        }
        std::string_view line(pageTextUtf8.data() + lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        std::string_view trimmed = line;
        const size_t indent = trimmed.find_first_not_of(" \t");
        if (indent != std::string_view::npos) {
            trimmed.remove_prefix(indent);
        }
        if (trimmed.rfind(subTag, 0) == 0) {
            const std::string name = LeadingIdentifier(trimmed.substr(subTag.size()));
            inBlock = _stricmp(name.c_str(), subUtf8.c_str()) == 0;
            found = found || inBlock;
            continue;
        }
        if (inBlock) {
            blockCompactUtf8 += CompactStatement(line);
            blockCompactUtf8.push_back('\n');
        }
    }
    return found;
}

// e5.95 materialises its subroutine index lazily, so a block appended through
// the hook's native paste can stay invisible to FN_GET_PRG_TEXT until
// something walks the page. A single scan that misses it reads as "absent",
// and creating on that answer appends another copy at the bottom - the very
// region the next scan cannot see either, so duplicates compound silently.
// Hence a three-state answer: absence only counts when the page proved it can
// still read back other subroutine names.
enum class SubLookupState {
    Found,
    // The page text proves the block exists but no physical row could be
    // resolved for it. Callers must treat this as "exists" - creating again
    // would append a duplicate - while skipping anything that needs a row.
    FoundTextOnly,
    ConfirmedAbsent,
    Unreadable,
};

struct SubLookupResult {
    SubLookupState state = SubLookupState::Unreadable;
    int row = -1;
    int duplicateRows = 0;
    // The page source this answer was derived from. Handed back so the rest of
    // the event can answer its own "does this line exist" questions from it
    // instead of select-alling the page again per question.
    std::string pageTextUtf8;
};

// Unlike ScanPageForType, which returns on the first exact match by design,
// this walks the whole page so same-named blocks can be counted. Thorough mode
// widens the "consecutive empty rows means end of page" heuristic: that bound
// is only a guess about how densely the grid indexes rows, and guessing low
// reports a subroutine as absent when it merely sits past the gap.
void CollectSubroutineRows(
    const std::string& subNameAnsi,
    std::vector<int>& matchingRows,
    int& nonEmptyNameCells,
    int& lastRowSeen,
    bool thorough = false)
{
    matchingRows.clear();
    nonEmptyNameCells = 0;
    lastRowSeen = -1;
    const int missingRowLimit =
        thorough ? kThoroughMissingRowsToStop : kMaximumMissingRowsToStop;
    int consecutiveMissingRows = 0;
    for (int row = 0; row < kMaximumRowsToScan; ++row) {
        bool rowExists = false;
        for (int column = 0; column < kMaximumColumnsToScan; ++column) {
            CellText cell;
            if (!ReadCell(row, column, cell) || !CellHasData(cell)) {
                continue;
            }
            rowExists = true;
            lastRowSeen = row;
            if (cell.type != VT_SUB_NAME || cell.text.empty()) {
                continue;
            }
            ++nonEmptyNameCells;
            if (_stricmp(cell.text.c_str(), subNameAnsi.c_str()) == 0) {
                matchingRows.push_back(row);
            }
        }
        consecutiveMissingRows = rowExists ? 0 : consecutiveMissingRows + 1;
        if (consecutiveMissingRows >= missingRowLimit) {
            break;
        }
    }
}

// FN_GET_PRG_TEXT only answers for rows the grid has materialised, which is
// roughly the visible window: on a page taller than the editor every row past
// the fold reads back empty no matter how long you wait, which is why a plain
// top-down scan finds two subroutines on a five-subroutine page. The caret does
// reach those rows though, so take the document's real height from
// FN_MOVE_BOTTOM and drag the readable window down over it in overlapping
// steps. Rows that read empty stay eligible for a later window - marking them
// visited is what would re-create the original blindness.
void CollectSubroutineRowsWindowed(
    const std::string& subNameAnsi,
    std::vector<int>& matchingRows,
    int& nonEmptyNameCells,
    int& lastRowSeen,
    int& documentLastRow)
{
    matchingRows.clear();
    nonEmptyNameCells = 0;
    lastRowSeen = -1;
    documentLastRow = -1;
    int savedRow = -1;
    int savedColumn = -1;
    QueryCaret(savedRow, savedColumn);
    InvokeIde(FN_MOVE_BOTTOM, 0, 0);
    int bottomRow = -1;
    int bottomColumn = -1;
    QueryCaret(bottomRow, bottomColumn);
    documentLastRow = bottomRow;
    if (bottomRow < 0) {
        return;
    }
    if (bottomRow >= kMaximumRowsToScan) {
        bottomRow = kMaximumRowsToScan - 1;
    }
    constexpr int kWindowStep = 24;
    constexpr int kWindowReach = 48;
    std::vector<char> hasData(static_cast<size_t>(bottomRow) + 1, 0);
    for (int base = 0; base <= bottomRow; base += kWindowStep) {
        InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(base), 0);
        PumpMessagesFor(20);
        const int first = base > kWindowReach ? base - kWindowReach : 0;
        const int last = base + kWindowReach < bottomRow ? base + kWindowReach : bottomRow;
        for (int row = first; row <= last; ++row) {
            if (hasData[static_cast<size_t>(row)] != 0) {
                continue;
            }
            for (int column = 0; column < kMaximumColumnsToScan; ++column) {
                CellText cell;
                if (!ReadCell(row, column, cell) || !CellHasData(cell)) {
                    continue;
                }
                hasData[static_cast<size_t>(row)] = 1;
                if (row > lastRowSeen) {
                    lastRowSeen = row;
                }
                if (cell.type != VT_SUB_NAME || cell.text.empty()) {
                    continue;
                }
                ++nonEmptyNameCells;
                if (_stricmp(cell.text.c_str(), subNameAnsi.c_str()) == 0) {
                    matchingRows.push_back(row);
                }
            }
        }
    }
    std::sort(matchingRows.begin(), matchingRows.end());
    if (savedRow >= 0 && savedColumn >= 0) {
        InvokeIde(
            FN_MOVE_CARET, static_cast<DWORD>(savedRow), static_cast<DWORD>(savedColumn));
    }
}

// What the page itself says after a memory-bridge paste.
//
// The grid used to be asked first, and it answers for materialised rows only:
// a landed write read back as absent, which reported a completed insertion as a
// failure and abandoned the rest of the event - no parameters, no body, no
// subscription line. Worse, the escalation that followed drove the caret to both
// ends of the page up to three times per click, which is the caret movement
// visible during a write. The page source settles presence in one round trip,
// and the grid is consulted only for the row number a jump needs.
struct MemoryWriteCheck {
    // The page's own source text carries the block.
    bool present = false;
    // The page could be rendered at all; when false, absence is unproven.
    bool pageReadable = false;
    int row = -1;
    std::string pageTextUtf8;
};

MemoryWriteCheck ConfirmMemoryWrite(
    const std::string& subNameAnsi,
    const std::string& knownPageTextUtf8 = std::string())
{
    MemoryWriteCheck out;
    const std::string subUtf8 = WideToUtf8(AnsiToWide(subNameAnsi));
    int copies = 0;
    int textLine = -1;
    out.pageTextUtf8 = knownPageTextUtf8;
    if (!out.pageTextUtf8.empty() || ReadPageCodeUtf8(out.pageTextUtf8)) {
        const PageCodeInfo page = ParsePageCode(out.pageTextUtf8);
        if (page.valid) {
            out.pageReadable = true;
            copies = CountSubInPage(page, subUtf8);
            out.present = copies > 0;
            textLine = SubLineFromPageText(page, subUtf8);
        }
    }
    if (!out.pageReadable) {
        DesignerLog::Write("HYBRID memory_verify source=unreadable action=stop_without_fallback");
        return out;
    }
    // Cell reads below resolve navigation only after memory source proves presence.
    out.row = ScanPageForType(VT_SUB_NAME, subNameAnsi, false).matchRow;
    // A block just pasted below the fold is routinely absent from the grid, and
    // reporting row=-1 for it left the caret to be placed by a full page walk at
    // the end of the event. The text already said which line it is on.
    if (out.row < 0 && copies == 1) {
        out.row = ResolveRowNearTextLine(subNameAnsi, textLine);
    }
    DesignerLog::Write(
        "HYBRID memory_verify source=page_text present=" +
        std::to_string(out.present ? 1 : 0) + " row=" + std::to_string(out.row) +
        " page_bytes=" + std::to_string(out.pageTextUtf8.size()) +
        " sub=\"" + WideToUtf8(AnsiToWide(subNameAnsi)) + "\"");
    return out;
}

// knownPageTextUtf8 lets a caller that already rendered this page hand the
// text down: two lookups in one event otherwise render the same page twice for
// answers that cannot have changed in between.
SubLookupResult LookupSubroutine(
    HWND mdiClient,
    HWND document,
    const std::string& subNameAnsi,
    const std::string& knownPageTextUtf8 = std::string())
{
    SubLookupResult out;
    const std::string subUtf8 = WideToUtf8(AnsiToWide(subNameAnsi));

    // Primary oracle: the page's own source text. It answers "does this exist,
    // and how many times" exactly, which a grid walk can only approximate.
    std::string pageText = knownPageTextUtf8;
    PageCodeInfo page;
    if (!pageText.empty() || ReadPageCodeUtf8(pageText)) {
        page = ParsePageCode(pageText);
        out.pageTextUtf8 = pageText;
    }
    const int textCopies = page.valid ? CountSubInPage(page, subUtf8) : -1;

    // The grid walk stays, because every writer downstream needs a physical
    // row number and the page text carries none. It is skipped outright when
    // the page parsed cleanly and never mentions the name: that settles the
    // question on its own, and the walk would spend a few hundred cell reads
    // per click only to agree.
    std::vector<int> rows;
    int nameCells = 0;
    int lastRow = -1;
    int attempts = 0;
    int documentLastRow = -1;
    if (textCopies > 0) {
        CollectSubroutineRows(subNameAnsi, rows, nameCells, lastRow);
        if (rows.empty() && textCopies == 1) {
            // The text has both the proof and the position, so the row costs one
            // caret move towards the target. This is the common case for any
            // subroutine sitting away from wherever the caret was last left, and
            // sending the window walk after it is what made a click on an
            // already-written handler take seconds of visible caret travel.
            const int resolved = ResolveRowNearTextLine(
                subNameAnsi, SubLineFromPageText(page, subUtf8));
            if (resolved >= 0) {
                rows.push_back(resolved);
            }
        }
        // Navigation only: source presence was already proven by memory. A read
        // failure never reaches this branch and never becomes permission to write.
        const bool rowStillOwed = textCopies > 0;
        // The resolved single copy is the case worth being frugal about; the
        // others are already rare or already broken.
        const int maximumAttempts = textCopies == 1 ? 1 : 2;
        for (int attempt = 1;
             rows.empty() && rowStillOwed && attempt <= maximumAttempts;
             ++attempt) {
            if (attempt == 2 && document != nullptr && IsWindow(document)) {
                ActivateDocument(mdiClient, document);
                PumpMessagesFor(150);
            }
            CollectSubroutineRowsWindowed(
                subNameAnsi, rows, nameCells, lastRow, documentLastRow);
            attempts = attempt;
        }
    }

    const int gridCopies = static_cast<int>(rows.size());
    if (!rows.empty()) {
        out.state = SubLookupState::Found;
        out.row = rows.front();
        out.duplicateRows = textCopies > gridCopies ? textCopies : gridCopies;
    }
    else if (textCopies > 0) {
        // The text proves the block exists even though no row could be
        // resolved. This is "exists", not "unknown": treating it as unknown is
        // what made a completed write report failure.
        out.state = SubLookupState::FoundTextOnly;
        out.duplicateRows = textCopies;
    }
    else if (textCopies == 0) {
        out.state = SubLookupState::ConfirmedAbsent;
    }
    else {
        out.state = SubLookupState::Unreadable;
    }
    DesignerLog::Write(
        "UI_EVENT sub_lookup name=\"" + subUtf8 +
        "\" state=" + std::string(
            out.state == SubLookupState::Found
                ? "found"
                : out.state == SubLookupState::FoundTextOnly
                    ? "found_text_only"
                    : out.state == SubLookupState::ConfirmedAbsent ? "absent" : "unreadable") +
        " row=" + std::to_string(out.row) +
        " copies=" + std::to_string(out.duplicateRows) +
        " text_copies=" + std::to_string(textCopies) +
        " text_subs=" + std::to_string(page.subNamesUtf8.size()) +
        " page_asm=\"" + page.assemblyUtf8 + "\"" +
        " attempts=" + std::to_string(attempts) +
        " name_cells=" + std::to_string(nameCells) +
        " last_row=" + std::to_string(lastRow) +
        " doc_rows=" + std::to_string(documentLastRow));
    if (out.duplicateRows > 1) {
        std::string rowList;
        for (const int row : rows) {
            if (!rowList.empty()) {
                rowList += ",";
            }
            rowList += std::to_string(row);
        }
        DesignerLog::Write(
            "UI_EVENT sub_duplicates name=\"" + subUtf8 +
            "\" count=" + std::to_string(out.duplicateRows) + " rows=" + rowList +
            " 警告：同名子程序存在多份，请手动删除多余的");
    }
    if (out.state == SubLookupState::Unreadable) {
        DumpPageHead(48);
    }
    return out;
}

bool CreateSubroutineAtPage(const std::string& subNameAnsi)
{
    // Use only the IDE-native command for creating a subroutine. The former
    // implementation first inserted a textual ".???" line and, when the
    // lazy code-grid index did not expose it immediately, fell through to this
    // command. In e5.95 that textual line could be accepted successfully and
    // become visible a few seconds later, so the fallback created a duplicate
    // subroutine (and could leave the editor in a bad state). The native
    // command already creates a valid subroutine row; rename that row after the
    // IDE has finished rebuilding its index.
    const int subCountBefore = CountCellsOfType(VT_SUB_NAME);
    DesignerLog::Write(
        "UI_EVENT create_sub begin native_only subs_before=" +
        std::to_string(subCountBefore));

    const bool commandInvoked = InvokeIde(FN_INSERT_NEW_SUB, 0, 0) != FALSE;
    DesignerLog::Write(
        "UI_EVENT create_sub native_command invoke=" +
        std::to_string(commandInvoked ? 1 : 0));
    if (!commandInvoked) {
        DumpPageHead(8);
        return false;
    }

    int subCountAfter = subCountBefore;
    for (int verifyAttempt = 0;
         verifyAttempt < 20 && subCountAfter <= subCountBefore;
         ++verifyAttempt) {
        PumpMessagesFor(250);
        subCountAfter = CountCellsOfType(VT_SUB_NAME);
    }
    DesignerLog::Write(
        "UI_EVENT create_sub native_only subs " +
        std::to_string(subCountBefore) + "->" +
        std::to_string(subCountAfter));
    if (subCountAfter <= subCountBefore) {
        DumpPageHead(8);
        return false;
    }

    // The native command appends the new subroutine at the bottom. Locate the
    // last real VT_SUB_NAME cell without moving/selecting the page again.
    PageScanResult lastSub;
    int consecutiveMissingRows = 0;
    for (int currentRow = 0; currentRow < kMaximumRowsToScan; ++currentRow) {
        bool rowExists = false;
        for (int currentColumn = 0;
             currentColumn < kMaximumColumnsToScan;
             ++currentColumn) {
            CellText cell;
            if (!ReadCell(currentRow, currentColumn, cell) || !CellHasData(cell)) {
                continue;
            }
            rowExists = true;
            if (cell.type == VT_SUB_NAME) {
                lastSub.matchRow = currentRow;
                lastSub.matchColumn = currentColumn;
                lastSub.matchText = cell.text;
            }
        }
        consecutiveMissingRows = rowExists ? 0 : consecutiveMissingRows + 1;
        if (consecutiveMissingRows >= kMaximumMissingRowsToStop) {
            break;
        }
    }
    if (lastSub.matchRow < 0) {
        DesignerLog::Write("UI_EVENT create_sub native_only new_sub_cell=none");
        return false;
    }
    DesignerLog::Write(
        "UI_EVENT create_sub native_only new_sub_cell=\"" +
        WideToUtf8(AnsiToWide(lastSub.matchText)) + "\" row=" +
        std::to_string(lastSub.matchRow) + " col=" +
        std::to_string(lastSub.matchColumn));

    const bool renamed = RenameCellAt(lastSub.matchRow, lastSub.matchColumn, subNameAnsi);
    if (!renamed) {
        return false;
    }
    for (int verifyAttempt = 0; verifyAttempt < 8; ++verifyAttempt) {
        CellText renamedCell;
        if (ReadCell(lastSub.matchRow, lastSub.matchColumn, renamedCell) &&
            _stricmp(renamedCell.text.c_str(), subNameAnsi.c_str()) == 0) {
            DesignerLog::Write(
                "UI_EVENT create_sub native_only rename_verified row=" +
                std::to_string(lastSub.matchRow));
            return true;
        }
        PumpMessagesFor(100);
    }
    DesignerLog::Write(
        "UI_EVENT create_sub native_only rename_unverified row=" +
        std::to_string(lastSub.matchRow));
    return renamed;
}

// Renames the assembly of the active page: moves the caret onto the
// VT_MOD_NAME cell and commits the new name through
// FN_SET_AND_COMPILE_PRG_ITEM_TEXT - the same entry the IDE's own name-cell
// editor commits through (e5.95 0x0044D0B0). Verified by cell text read-back
// and, as a fallback that does not depend on text reading, the MDI title.
bool RenameAssemblyViaApi(HWND targetDocument, const std::string& newNameAnsi)
{
    const PageScanResult nameCell = ScanPageForType(VT_MOD_NAME, std::string(), true);
    if (nameCell.matchRow < 0) {
        DesignerLog::Write(
            "UI_EVENT rename_api locate=0 cells=" +
            std::to_string(nameCell.typeCellCount));
        return false;
    }
    DesignerLog::Write(
        "UI_EVENT rename_api target=\"" +
        WideToUtf8(AnsiToWide(nameCell.matchText)) + "\" row=" +
        std::to_string(nameCell.matchRow) + " col=" +
        std::to_string(nameCell.matchColumn));
    const bool cellRenamed =
        RenameCellAt(nameCell.matchRow, nameCell.matchColumn, newNameAnsi);
    PumpMessagesFor(150);
    const std::wstring title = GetWindowTitle(targetDocument);
    const bool titleMatched = ProjectAssembly::MatchesDocumentTitle(title, AnsiToWide(newNameAnsi));
    DesignerLog::Write(
        "UI_EVENT rename_api cell_renamed=" + std::to_string(cellRenamed ? 1 : 0) +
        " title_verified=" + std::to_string(titleMatched ? 1 : 0) +
        " title=\"" + WideToUtf8(title) + "\"");
    return cellRenamed || titleMatched;
}

IdeEventRouter::RouteResult Fail(std::string action, std::string message)
{
    return {false, std::move(action), std::move(message)};
}

// ---------- code-page statement helpers (Jade 通讯/注册 call generation) ----------

std::string CompactStatement(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char character : text) {
        if (character != ' ' && character != '\t' &&
            character != '\r' && character != '\n') {
            out.push_back(character);
        }
    }
    return out;
}

bool IsSubscriptionStatement(
    std::string_view line,
    std::string_view channelAnsi,
    std::string_view handlerAnsi)
{
    const std::string compact = CompactStatement(line);
    if (compact.find("JadeView.") == std::string::npos ||
        compact.find(channelAnsi) == std::string::npos) {
        return false;
    }
    return handlerAnsi.empty() || compact.find(handlerAnsi) != std::string::npos;
}

// Whether the fixed subscription routine already carries this channel's line,
// judged from the page source. The grid path below has to scroll the page to the
// top to read those rows, which on an already-wired button is pure cost the user
// sees as the caret hunting around before it lands. Note the IDE serialises
// string literals with full-width quotes, so the channel and &handler are
// matched on their own rather than through a quoted token.
bool PageTextHasSubscription(
    const std::string& pageTextUtf8,
    const std::string& subscribeSubUtf8,
    const std::string& channelUtf8,
    const std::string& handlerUtf8);

struct StatementArea {
    std::vector<std::pair<int, std::string>> lines; // (row, compact text)
    int lastRow = -1;
};

bool PageTextHasSubscription(
    const std::string& pageTextUtf8,
    const std::string& subscribeSubUtf8,
    const std::string& channelUtf8,
    const std::string& handlerUtf8)
{
    std::string block;
    if (pageTextUtf8.empty() ||
        !SubBlockCompactFromPageText(pageTextUtf8, subscribeSubUtf8, block)) {
        return false;
    }
    size_t lineStart = 0;
    while (lineStart < block.size()) {
        size_t lineEnd = block.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
            lineEnd = block.size();
        }
        const std::string_view line(block.data() + lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        if (IsSubscriptionStatement(line, channelUtf8, handlerUtf8)) {
            return true;
        }
    }
    return false;
}

// Collects the statement lines that belong to the subroutine whose name cell
// sits at subNameRow: every following row up to the next "title" row (the
// next sub header) or the end of the page.
StatementArea ScanSubStatementArea(int subNameRow)
{
    StatementArea area;
    int consecutiveMissingRows = 0;
    for (int row = subNameRow + 1; row < kMaximumRowsToScan; ++row) {
        CellText first;
        if (ReadCell(row, 0, first) && CellHasData(first) && first.isTitle &&
            first.type == VT_SUB_NAME) {
            break;
        }
        bool rowExists = false;
        std::string lineText;
        for (int column = 0; column < kMaximumColumnsToScan; ++column) {
            CellText cell;
            if (!ReadCell(row, column, cell) || !CellHasData(cell)) {
                continue;
            }
            rowExists = true;
            if (column == 0 && !cell.text.empty()) {
                lineText = cell.text;
            }
        }
        if (rowExists) {
            area.lastRow = row;
            if (!lineText.empty()) {
                area.lines.push_back({row, CompactStatement(lineText)});
            }
        }
        consecutiveMissingRows = rowExists ? 0 : consecutiveMissingRows + 1;
        if (consecutiveMissingRows >= kMaximumMissingRowsToStop) {
            break;
        }
    }
    return area;
}

// Inserts one or more statement lines into the given subroutine when none of
// them already contains detectToken. knownRow comes straight from the
// caller's just-verified page scan; re-locating by name right after a
// create/rename races with the IDE's lazy subroutine index rebuild, so the
// known row is trusted (after cell-text verification) and name lookup is only
// a fallback. When insertBeforeToken is non-empty and found inside the sub,
// the statement is inserted BEFORE that line instead of appended (used so
// JadeView.App.注册事件 lines stay above the 初始化 block).
bool AppendStatementsIfMissing(
    HWND mdiClient,
    HWND document,
    const std::string& subNameAnsi,
    const std::string& statementText,
    const std::string& detectToken,
    int knownRow,
    const std::string& insertBeforeToken = std::string())
{
    const std::string subscribeNameAnsi = WideToAnsi(L"通讯.订阅");
    const bool isSubscription =
        detectToken.find("JadeView.") != std::string::npos &&
        detectToken.find(subscribeNameAnsi) != std::string::npos;
    const std::string compactDetectToken = CompactStatement(detectToken);
    const std::string subscriptionKey = subNameAnsi + "\n" + compactDetectToken;
    int nameRow = knownRow;
    if (nameRow >= 0) {
        CellText verify{};
        if (ReadCell(nameRow, 0, verify) &&
            _stricmp(verify.text.c_str(), subNameAnsi.c_str()) == 0) {
            DesignerLog::Write(
                "UI_EVENT append_statement known_row=" + std::to_string(nameRow) + " ok=1");
        }
        else {
            DesignerLog::Write(
                "UI_EVENT append_statement known_row=" + std::to_string(nameRow) +
                " mismatch=\"" + WideToUtf8(AnsiToWide(verify.text)) + "\"");
            nameRow = -1;
        }
    }
    if (nameRow < 0) {
        // Re-activate the target document and let the IDE settle its editor
        // context before scanning; a pending MDI activation can otherwise
        // point FN_GET_PRG_TEXT at the wrong page mid-flow.
        if (document != nullptr && IsWindow(document)) {
            ActivateDocument(mdiClient, document);
            PumpMessagesFor(150);
        }
        for (int attempt = 0; attempt < 8; ++attempt) {
            const PageScanResult subLocate = ScanPageForType(VT_SUB_NAME, subNameAnsi, false);
            if (subLocate.matchRow >= 0) {
                nameRow = subLocate.matchRow;
                break;
            }
            PumpMessagesFor(150);
        }
    }
    if (nameRow < 0) {
        DumpPageHead(12);
        DesignerLog::Write(
            "UI_EVENT append_statement locate=0 sub=\"" +
            WideToUtf8(AnsiToWide(subNameAnsi)) + "\"");
        return false;
    }
    const StatementArea before = ScanSubStatementArea(nameRow);
    std::string subscriptionChannel;
    std::string subscriptionHandler;
    if (isSubscription) {
        const size_t channelStart = statementText.find('"');
        const size_t channelEnd = channelStart == std::string::npos
            ? std::string::npos
            : statementText.find('"', channelStart + 1);
        const size_t handlerStart = statementText.find('&');
        const size_t handlerEnd = handlerStart == std::string::npos
            ? std::string::npos
            : statementText.find(')', handlerStart + 1);
        if (channelStart != std::string::npos && channelEnd != std::string::npos) {
            subscriptionChannel = statementText.substr(
                channelStart + 1, channelEnd - channelStart - 1);
        }
        if (handlerStart != std::string::npos) {
            subscriptionHandler = statementText.substr(
                handlerStart + 1,
                handlerEnd == std::string::npos
                    ? std::string::npos
                    : handlerEnd - handlerStart - 1);
        }
    }
    bool matchingSubscriptionKept = false;
    bool statementExists = false;
    for (const auto& entry : before.lines) {
        const bool subscriptionExists =
            isSubscription &&
            IsSubscriptionStatement(
                entry.second,
                subscriptionChannel,
                subscriptionHandler);
        if (entry.second.find(compactDetectToken) != std::string::npos || subscriptionExists) {
            if (subscriptionExists && matchingSubscriptionKept) {
                RenameCellAt(entry.first, 0, std::string());
                DesignerLog::Write(
                    "UI_EVENT append_statement duplicate_removed row=" +
                    std::to_string(entry.first));
                continue;
            }
            matchingSubscriptionKept = matchingSubscriptionKept || subscriptionExists;
            statementExists = true;
        }
    }
    if (statementExists) {
        if (isSubscription) {
            g_writtenSubscriptionKeys.insert(subscriptionKey);
        }
        DesignerLog::Write(
            "UI_EVENT append_statement exists=1 sub=\"" +
            WideToUtf8(AnsiToWide(subNameAnsi)) + "\"");
        return true;
    }

    // Insertion target: the row of insertBeforeToken when requested, else a
    // blank statement row inside the subroutine (bottom-up). Blank statement
    // rows (t=851) cannot be targeted directly with FN_MOVE_CARET - the IDE
    // clamps the caret back to the previous content row - so after moving we
    // may need FN_MOVE_DOWN or FN_MOVE_BOTTOM to reach them.
    int insertRow = -1;
    bool insertBeforeNonEmpty = false;
    if (!insertBeforeToken.empty()) {
        for (const auto& entry : before.lines) {
            if (entry.second.find(insertBeforeToken) != std::string::npos) {
                insertRow = entry.first;
                insertBeforeNonEmpty = true;
                break;
            }
        }
    }
    if (insertRow < 0) {
        for (int row = before.lastRow; row > nameRow; --row) {
            CellText probe{};
            if (ReadCell(row, 0, probe) && !probe.isTitle && probe.text.empty()) {
                insertRow = row;
                break;
            }
        }
    }
    if (insertRow < 0) {
        insertRow = (before.lastRow >= 0 ? before.lastRow : nameRow) + 1;
    }
    // A subroutine at the end of a page may have no physical trailing row.
    // Create exactly one local row after its last statement before moving the
    // caret; FN_MOVE_CARET alone clamps nonexistent rows to the previous line.
    if (before.lastRow >= 0 && insertRow > before.lastRow && !insertBeforeNonEmpty) {
        InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(before.lastRow), 0);
        int landedRow = -1;
        int landedColumn = -1;
        QueryCaret(landedRow, landedColumn);
        if (landedRow == before.lastRow) {
            InvokeIde(FN_INSERT_NEW_AT_NEXT, 0, 0);
            PumpMessagesFor(kInsertSettleMs);
        }
    }
    DesignerLog::Write(
        "UI_EVENT append_statement insert_row=" + std::to_string(insertRow) +
        " before_content=" + std::to_string(insertBeforeNonEmpty ? 1 : 0));

    int caretRow = -1;
    int caretColumn = -1;
    auto caretAt = [&](int wantedRow) {
        QueryCaret(caretRow, caretColumn);
        return caretRow == wantedRow;
    };
    InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(insertRow), 0);
    if (!caretAt(insertRow) && !insertBeforeNonEmpty) {
        InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(insertRow - 1), 0);
        InvokeIde(FN_MOVE_DOWN, 0, 0);
        if (!caretAt(insertRow)) {
            InvokeIde(FN_MOVE_BOTTOM, 0, 0);
            if (!caretAt(insertRow)) {
                DesignerLog::Write(
                    "UI_EVENT append_statement caret_mismatch want=" +
                    std::to_string(insertRow) + " got=" + std::to_string(caretRow) + "/" +
                    std::to_string(caretColumn));
                return false;
            }
        }
    }
    CellText landed{};
    ReadCell(caretRow, 0, landed);
    if (!insertBeforeNonEmpty && !landed.text.empty()) {
        DesignerLog::Write(
            "UI_EVENT append_statement landed_not_empty row=" +
            std::to_string(caretRow) + " text=\"" +
            WideToUtf8(AnsiToWide(landed.text)) + "\"");
        return false;
    }
    InvokeIde(FN_MOVE_EDIT_CARET_TO_END, 0, 0);
    std::string writeError;
    if (!HookBridge::InsertAnsi(statementText, writeError)) {
        DesignerLog::Write("UI_EVENT append_statement memory_write_failed error=\"" + writeError + "\"");
        return false;
    }
    PumpMessagesFor(kInsertSettleMs);
    // e5.95 can lag when rebuilding the statement grid: the write has landed
    // even though the immediate cell read still returns the old row. Do not
    // submit the same subscription again on the next click.
    if (isSubscription) {
        g_writtenSubscriptionKeys.insert(subscriptionKey);
    }

    const StatementArea after = ScanSubStatementArea(nameRow);
    for (const auto& entry : after.lines) {
        const bool subscriptionInserted =
            isSubscription &&
            IsSubscriptionStatement(
                entry.second,
                subscriptionChannel,
                subscriptionHandler);
        if (entry.second.find(compactDetectToken) != std::string::npos || subscriptionInserted) {
            if (isSubscription) {
                g_writtenSubscriptionKeys.insert(subscriptionKey);
            }
            DesignerLog::Write(
                "UI_EVENT append_statement inserted=1 sub=\"" +
                WideToUtf8(AnsiToWide(subNameAnsi)) + "\"");
            return true;
        }
    }
    DesignerLog::Write(
        "UI_EVENT append_statement verify=0 sub=\"" +
        WideToUtf8(AnsiToWide(subNameAnsi)) + "\"");
    return false;
}

// Ensures the fixed UI_启动JadeView subroutine contains the full JadeView
// startup skeleton: the native lifecycle 注册事件 lines (which must precede
// 初始化), the 初始化 block with EMPTY app name / identifier (never changed
// when present), and the 消息循环 tail. The lifecycle callback subroutines are
// created as empty shells inside the same assembly; the user adds their
// 参数 declarations (窗口id:整数型, 数据:文本型) manually.
struct PageEnsureResult {
    bool ok = false;
    HWND document = nullptr;
    bool createdAssembly = false;
    bool createdSubroutine = false;
    int subRow = -1;
    std::string message;
};

PageEnsureResult EnsureAssemblySubPage(
    HWND mainWindow,
    HWND mdiClient,
    const std::wstring& assembly,
    const std::wstring& subName,
    const std::string& initialSource = std::string());
void EnsureLifecycleCallbackBody(
    HWND mainWindow,
    HWND mdiClient,
    HWND document,
    const std::string& eventNameAnsi,
    const std::wstring& subNameWide,
    int subRow);
bool EnsureJadeViewStartupSkeleton(
    HWND mainWindow,
    HWND mdiClient,
    HWND document,
    const std::string& subNameAnsi,
    int knownRow)
{
    struct NativeEvent {
        std::wstring_view eventName;
        std::wstring_view callbackName;
    };
    static constexpr NativeEvent kNativeEvents[] = {
        {L"app-ready", L"JadeView准备就绪"},
        {L"tray-event", L"事件_托盘点击"},
        {L"window-created", L"事件_某窗口创建完毕"},
        {L"window-all-closed", L"事件_所有窗口关闭"},
        {L"window-closed", L"事件_某窗口关闭"},
    };

    const std::string startupSubAnsi = WideToAnsi(kRegisterSub);
    const PageScanResult hookLocate = ScanPageForType(VT_SUB_NAME, startupSubAnsi, false);
    const StatementArea area = hookLocate.matchRow >= 0
        ? ScanSubStatementArea(hookLocate.matchRow)
        : StatementArea{};
    bool hasInit = false;
    for (const auto& entry : area.lines) {
        if (entry.second.find(WideToAnsi(L"JadeView.App.初始化")) != std::string::npos) {
            hasInit = true;
            break;
        }
    }
    if (hasInit) {
        DesignerLog::Write(
            "UI_EVENT startup_skeleton exists=1 sub=\"" + WideToUtf8(AnsiToWide(subNameAnsi)) +
            "\"（已有初始化块，未改动）");
        return true;
    }

    // First-time skeleton, inserted atomically: the Jade_通讯_订阅 hook call
    // (must run before 初始化, mirroring 订阅IPC消息() in the reference
    // project), the five native 注册事件 lines, and the 初始化/消息循环 tail.
    std::wstring skeletonWide = L"Jade_通讯_订阅 ()\r\n";
    for (const NativeEvent& nativeEvent : kNativeEvents) {
        skeletonWide += std::wstring(L"JadeView.App.注册事件 (\"") +
            std::wstring(nativeEvent.eventName) +
            L"\", &" + std::wstring(nativeEvent.callbackName) + L")\r\n";
    }
    skeletonWide += L"' 初始化参数：①是否开启DevTools（调试改 真）②日志文件路径 ③应用名 ④应用唯一标识\r\n"
        L"初始化成功 ＝ JadeView.App.初始化 (假, \"\", 取运行目录 (), \"\", \"\", 假)\r\n"
        L".如果真 (初始化成功 ＝ 假)\r\n"
        L"\r\n"
        L"    调试输出 (\"JadeView 初始化失败：已有程序实例或运行环境被占用\")\r\n"
        L"    返回 ()\r\n"
        L".如果真结束\r\n"
        L"\r\n"
        L"调试输出 (\"JadeView 初始化请求已接受，等待就绪事件\")\r\n"
        L"JadeView.App.消息循环 ()\r\n";
    const std::string skeleton = WideToAnsi(skeletonWide);
    const bool initReady = AppendStatementsIfMissing(
        mdiClient, document, startupSubAnsi, skeleton,
        WideToAnsi(L"JadeView.App.初始化"), knownRow);

    // Create the lifecycle callback shells and fill their production
    // signatures/bodies. 参数 declarations: user adds 窗口id/数据 types if the
    // auto .参数 lines did not register.
    for (const NativeEvent& nativeEvent : kNativeEvents) {
        const PageEnsureResult shellPage = EnsureAssemblySubPage(
            mainWindow,
            mdiClient,
            std::wstring(kRegisterAssembly),
            std::wstring(nativeEvent.callbackName));
        if (shellPage.ok && shellPage.subRow >= 0) {
            EnsureLifecycleCallbackBody(
                mainWindow,
                mdiClient,
                shellPage.document,
                WideToAnsi(nativeEvent.eventName),
                std::wstring(nativeEvent.callbackName),
                shellPage.subRow);
        }
    }
    DesignerLog::Write(
        "UI_EVENT startup_skeleton created=1 init_ready=" +
        std::to_string(initReady ? 1 : 0) +
        " 提醒：若 .参数 行未自动变成参数声明，请手动补参数（窗口id:整数型，数据:文本型）");
    return initReady;
}

// ---------- callback signature/body generation (per real JadeView usage) ----------

struct SubParamSpec {
    const wchar_t* name;
    const wchar_t* type;
};


bool SubHasParamNamed(int nameRow, const std::string& paramAnsi)
{
    int consecutiveMissingRows = 0;
    for (int row = nameRow + 1; row < nameRow + 20 && row < kMaximumRowsToScan; ++row) {
        bool rowExists = false;
        for (int column = 0; column < 2; ++column) {
            CellText cell;
            if (!ReadCell(row, column, cell) || !CellHasData(cell)) {
                continue;
            }
            rowExists = true;
            if (cell.type == VT_SUB_ARG_NAME &&
                _stricmp(cell.text.c_str(), paramAnsi.c_str()) == 0) {
                return true;
            }
        }
        consecutiveMissingRows = rowExists ? 0 : consecutiveMissingRows + 1;
        if (consecutiveMissingRows >= 8) {
            break;
        }
    }
    return false;
}

int FindRowCellColumnByType(int row, int desiredType)
{
    for (int column = 0; column < kMaximumColumnsToScan; ++column) {
        CellText cell;
        if (ReadCell(row, column, cell) && cell.type == desiredType) {
            return column;
        }
    }
    return -1;
}

// Sets a subroutine's return-value type through the same name-cell commit
// path (FN_SET_AND_COMPILE_PRG_ITEM_TEXT on the type cell).
bool SetSubReturnType(int nameRow, const std::string& typeAnsi)
{
    const int typeColumn = FindRowCellColumnByType(nameRow, VT_SUB_RET_TYPE);
    if (typeColumn < 0) {
        DesignerLog::Write("UI_EVENT sub_ret_type locate=0 row=" + std::to_string(nameRow));
        return false;
    }
    CellText current{};
    if (ReadCell(nameRow, typeColumn, current) &&
        _stricmp(current.text.c_str(), typeAnsi.c_str()) == 0) {
        return true;
    }
    const bool renamed = RenameCellAt(nameRow, typeColumn, typeAnsi);
    CellText verify{};
    const bool verified = ReadCell(nameRow, typeColumn, verify) &&
        _stricmp(verify.text.c_str(), typeAnsi.c_str()) == 0;
    DesignerLog::Write(
        "UI_EVENT sub_ret_type set=" + std::to_string(renamed ? 1 : 0) +
        " verify=" + std::to_string(verified ? 1 : 0) +
        " row=" + std::to_string(nameRow) + " col=" + std::to_string(typeColumn));
    return verified;
}

// Gives a 通讯.订阅 callback subroutine its production signature:
// 参数 rows created with FN_INSERT_NEW_ARG (the IDE's own 插入子程序参数
// command - typed ".参数" text does NOT become a real parameter), plus the
// UTF-8 reminder comment, the 返回 response pointer and 整数型 return type -
// mirroring ipc_获取二维码 in the reference project.
bool EnsureSubParams(
    HWND mainWindow,
    HWND mdiClient,
    HWND document,
    const std::string& subNameAnsi,
    int subRow,
    const std::vector<SubParamSpec>& params)
{
    bool allPresent = true;
    for (const SubParamSpec& spec : params) {
        if (!SubHasParamNamed(subRow, WideToAnsi(spec.name))) {
            allPresent = false;
            break;
        }
    }
    if (allPresent) {
        return true;
    }

    // FN_INSERT_NEW_ARG was tested against e5.95 and does not create a real
    // argument row. Invoking it while a modal preview is being dismissed can
    // also leave the IDE editor context unstable. Do not run that unsupported
    // route or the old whole-page replacement fallback from the event callback.
    // The callback and subscription are still written safely; parameters can be
    // added manually until a dedicated argument-row API is verified.
    (void)mainWindow;
    (void)mdiClient;
    (void)document;
    (void)subRow;
    DesignerLog::Write(
        "UI_EVENT sub_param ok=0 method=deferred_safe_path sub=\"" +
        WideToUtf8(AnsiToWide(subNameAnsi)) +
        "\" reason=unsupported_native_argument_insert_skipped");
    return false;

}

// Earlier revisions typed ".参数" text into the statement area; those lines
// are plain text, not parameter declarations. Clear them so the corrected
// parameter rows stand alone.
void CleanupBogusParamStatements(const std::string& subNameAnsi, int subRow)
{
    (void)subNameAnsi;
    const StatementArea area = ScanSubStatementArea(subRow);
    int cleaned = 0;
    for (const auto& entry : area.lines) {
        if (entry.second.rfind(".参数", 0) == 0) {
            if (RenameCellAt(entry.first, 0, std::string())) {
                ++cleaned;
            }
        }
    }
    if (cleaned > 0) {
        DesignerLog::Write(
            "UI_EVENT bogus_param_statements_cleared count=" + std::to_string(cleaned));
    }
}

// Gives a 通讯.订阅 callback subroutine its production signature:
// .参数 WinId/msg lines, a UTF-8 reminder comment, the 返回 response pointer
// and 整数型 return type - mirroring ipc_获取二维码 in the reference project.
// bodyAlreadyWritten is set by the caller that had the hook paste the whole
// .子程序/.参数/函数体 block: that text is parsed and committed by the IDE's own
// parser, so the body is complete by construction and must not be re-derived
// from a page read.
void SetupSubscribeCallbackBody(
    HWND mainWindow,
    HWND mdiClient,
    HWND document,
    const std::wstring& subNameWide,
    int subRow,
    bool bodyAlreadyWritten = false,
    const std::string& knownPageTextUtf8 = std::string())
{
    const std::string subAnsi = WideToAnsi(subNameWide);
    if (subAnsi.empty() || subRow < 0) {
        // Every check below is anchored on a physical row, so an unresolved row
        // means the signature and body are never looked at - the subroutine is
        // left exactly as the paste made it. That used to show up in the log as
        // nothing at all, which reads like a step that passed.
        DesignerLog::Write(
            "UI_EVENT callback_body_unchecked sub=\"" + WideToUtf8(subNameWide) +
            "\" sub_row=" + std::to_string(subRow));
        return;
    }
    CleanupBogusParamStatements(subAnsi, subRow);

    // Every question below is "is this part of the signature already there".
    // The grid reader stops at the fold, so on a page taller than the editor it
    // answers "no" for a subroutine below it, and acting on that answer is what
    // wrote a second 返回 line - EnsureSubParams would just as happily add a
    // second 参数 row the same way. The page source has no fold, so read it once
    // here and let it decide all of them. The caller's copy is from this same
    // event, so re-reading would only select-all the page again for the same
    // answer.
    std::string pageText = knownPageTextUtf8;
    std::string blockCompact;
    // The caller reads the page once per event through the memory bridge and
    // hands the text down. Reading it again here would render the same page for
    // the same answer, and doing it from this depth is what used to move the
    // caret around while the user was still looking at the preview.
    const bool pageReadable = !pageText.empty();
    const bool blockFound = pageReadable &&
        SubBlockCompactFromPageText(pageText, WideToUtf8(subNameWide), blockCompact);

    const std::vector<SubParamSpec> params = {
        {L"WinId", L"整数型"}, {L"msg", L"文本型"}};
    // A block this event just pasted carries its own .参数 rows: the IDE parsed
    // the whole .子程序/.参数/函数体 text in one go, so it is complete by
    // construction and re-deriving it from a page read can only get it wrong.
    bool textHasAllParams = bodyAlreadyWritten || blockFound;
    if (!bodyAlreadyWritten) {
        for (const SubParamSpec& spec : params) {
            const std::string tag = WideToUtf8(std::wstring(L".参数") + spec.name + L",");
            if (blockCompact.find(tag) == std::string::npos) {
                textHasAllParams = false;
                break;
            }
        }
    }
    const bool paramsOk = textHasAllParams ||
        EnsureSubParams(mainWindow, mdiClient, document, subAnsi, subRow, params);
    // Page replacement inserts argument rows and shifts physical row numbers.
    // Refresh the callback row once before writing its body/type.
    const PageScanResult refreshedSub = ScanPageForType(VT_SUB_NAME, subAnsi, false);
    if (refreshedSub.matchRow >= 0) {
        subRow = refreshedSub.matchRow;
    }
    const std::string msgText = WideToAnsi(L"msg ＝ UTF8文本到GBK文本 (msg)\r\n");
    const std::string msgToken = WideToAnsi(L"UTF8文本到GBK文本(msg)");
    const std::string returnText =
        WideToAnsi(L"返回 (JadeView.文本.创建指针 (\"ok\"))\r\n");
    const std::string returnToken = WideToAnsi(L"JadeView.文本.创建指针");
    int keptMsgRow = -1;
    int keptReturnRow = -1;
    int bodyLineCount = 0;
    int bodyLastRow = -1;
    // Finds the two body lines and clears any copy beyond the first of each.
    // Re-runnable: a second pass has nothing left to clear.
    auto scanBody = [&]() {
        keptMsgRow = -1;
        keptReturnRow = -1;
        const StatementArea bodyArea = ScanSubStatementArea(subRow);
        bodyLineCount = static_cast<int>(bodyArea.lines.size());
        bodyLastRow = bodyArea.lastRow;
        for (const auto& entry : bodyArea.lines) {
            const bool hasMsg = entry.second.find(msgToken) != std::string::npos;
            const bool hasReturn = entry.second.find(returnToken) != std::string::npos;
            if (hasMsg && hasReturn) {
                RenameCellAt(entry.first, 0, std::string());
                continue;
            }
            if (hasMsg) {
                if (keptMsgRow >= 0) {
                    RenameCellAt(entry.first, 0, std::string());
                }
                else {
                    keptMsgRow = entry.first;
                }
            }
            if (hasReturn) {
                if (keptReturnRow >= 0) {
                    RenameCellAt(entry.first, 0, std::string());
                }
                else {
                    keptReturnRow = entry.first;
                }
            }
        }
    };
    if (bodyAlreadyWritten) {
        // Nothing to detect and nothing to append: appending here on a page the
        // reader could not fully see is what produced two 返回 lines in one
        // callback. Only the return type still needs asserting below.
        DesignerLog::Write(
            "UI_EVENT callback_body_skipped sub=\"" + WideToUtf8(subNameWide) +
            "\" reason=hook_wrote_body row=" + std::to_string(subRow));
    }
    else {
        // scanBody only clears same-line duplicates it can actually see; it must
        // not decide what is missing. The grid reader stops at the fold, so on a
        // page taller than the editor a body line below it reads as absent -
        // appending on that answer is what wrote a second 返回 into the middle of
        // a subroutine that already had one. The page's own source text has no
        // fold, so it makes the call.
        scanBody();
        const std::string msgTokenUtf8 = WideToUtf8(L"UTF8文本到GBK文本(msg)");
        const std::string returnTokenUtf8 = WideToUtf8(L"JadeView.文本.创建指针");
        const bool textHasMsg =
            blockFound && blockCompact.find(msgTokenUtf8) != std::string::npos;
        const bool textHasReturn =
            blockFound && blockCompact.find(returnTokenUtf8) != std::string::npos;
        DesignerLog::Write(
            "UI_EVENT callback_body_text sub=\"" + WideToUtf8(subNameWide) +
            "\" page_ok=" + std::to_string(pageReadable ? 1 : 0) +
            " block_found=" + std::to_string(blockFound ? 1 : 0) +
            " text_msg=" + std::to_string(textHasMsg ? 1 : 0) +
            " text_return=" + std::to_string(textHasReturn ? 1 : 0) +
            " grid_msg_row=" + std::to_string(keptMsgRow) +
            " grid_return_row=" + std::to_string(keptReturnRow) +
            " lines=" + std::to_string(bodyLineCount) +
            " last_row=" + std::to_string(bodyLastRow));
        if (blockFound) {
            if (!textHasMsg) {
                AppendStatementsIfMissing(
                    mdiClient, document, subAnsi, msgText, msgToken, subRow);
            }
            if (!textHasReturn) {
                AppendStatementsIfMissing(
                    mdiClient, document, subAnsi, returnText, returnToken, subRow);
            }
        }
        else {
            // Only a source that can see the whole page gets to say a line is
            // missing. The grid answers per materialised cell, so a body line
            // the editor has not laid out reads as absent no matter how many
            // times it is asked - and appending on that answer is what put a
            // second 返回 inside a subroutine that already had one. A line that
            // is genuinely missing is a one-line manual fix; a duplicate 返回
            // does not compile.
            DesignerLog::Write(
                "UI_EVENT callback_body_unverified sub=\"" + WideToUtf8(subNameWide) +
                "\" page_ok=" + std::to_string(pageReadable ? 1 : 0) +
                " append_skipped=1 reason=page_source_unavailable");
        }
    }
    const bool retTypeOk = SetSubReturnType(subRow, WideToAnsi(L"整数型"));
    DesignerLog::Write(
        "UI_EVENT subscribe_callback_body sub=\"" + WideToUtf8(subNameWide) +
        "\" params_ok=" + std::to_string(paramsOk ? 1 : 0) +
        " ret_type_ok=" + std::to_string(retTypeOk ? 1 : 0) +
        (paramsOk && retTypeOk ? "" : " 警告：参数/返回值未能自动写入，请手动补齐"));
}

// Fills a native lifecycle callback subroutine with its production signature
// (and body for the events that always carry one).
void EnsureLifecycleCallbackBody(
    HWND mainWindow,
    HWND mdiClient,
    HWND document,
    const std::string& eventNameAnsi,
    const std::wstring& subNameWide,
    int subRow)
{
    const std::string subAnsi = WideToAnsi(subNameWide);
    if (subAnsi.empty() || subRow < 0) {
        return;
    }
    std::vector<SubParamSpec> params;
    std::string bodyText;
    std::string bodyToken;
    std::string returnType;

    if (eventNameAnsi == WideToAnsi(L"app-ready")) {
        params = {{L"成功否", L"逻辑型"}, {L"err", L"文本型"}};
        bodyText = WideToAnsi(
            L".如果 (成功否)\r\n"
            L"    创建主窗口 ()\r\n"
            L".否则\r\n"
            L"    调试输出 (“JadeView初始化失败”, err)\r\n"
            L".如果结束\r\n");
        bodyToken = WideToAnsi(L"创建主窗口");
    }
    else if (eventNameAnsi == WideToAnsi(L"tray-event")) {
        params = {{L"WinID", L"整数型"}, {L"数据", L"文本型"}};
    }
    else if (eventNameAnsi == WideToAnsi(L"window-created")) {
        params = {{L"WinID", L"整数型"}, {L"数据", L"文本型"}};
        returnType = WideToAnsi(L"整数型");
    }
    else if (eventNameAnsi == WideToAnsi(L"window-all-closed")) {
        params = {{L"窗口ID", L"整数型"}, {L"数据", L"文本型"}};
        bodyText = WideToAnsi(L"JadeView.App.退出 ()\r\n");
        bodyToken = WideToAnsi(L"JadeView.App.退出");
    }
    else if (eventNameAnsi == WideToAnsi(L"window-closed")) {
        params = {{L"WinID", L"整数型"}, {L"数据", L"文本型"}};
    }
    else {
        params = {{L"WinId", L"整数型"}, {L"数据", L"文本型"}};
    }

    CleanupBogusParamStatements(subAnsi, subRow);
    EnsureSubParams(mainWindow, mdiClient, document, subAnsi, subRow, params);
    if (!bodyText.empty()) {
        AppendStatementsIfMissing(
            mdiClient, document, subAnsi, bodyText, bodyToken, subRow);
    }
    if (!returnType.empty()) {
        SetSubReturnType(subRow, returnType);
        AppendStatementsIfMissing(
            mdiClient, document, subAnsi,
            WideToAnsi(L"返回 (0)\r\n"),
            WideToAnsi(L"返回(0)"), subRow);
    }
}

// hintRow is the row a previous lookup resolved for this subroutine. Statements
// inserted above it since then shift it by a few lines, so it is a place to
// start looking, never a destination to jump to blindly.
constexpr int kJumpHintReach = 40;

// A plain scan only sees the rows the grid has materialised, so on a page taller
// than the editor every subroutine below the fold reads as absent and the jump
// silently does nothing - which is why the caret stopped following the later
// buttons. Moving the caret to the hint first brings that stretch of the page
// into view, and only then can its name cell be read and confirmed.
// Completes a jump when the caller already resolved the physical name cell.
// This intentionally does not scan, copy, select, or rewrite the page.
bool JumpToLocatedSubroutine(
    const std::string& subNameAnsi,
    int targetRow,
    int targetColumn)
{
    if (targetRow < 0 || targetColumn < 0) {
        return false;
    }
    InvokeIde(
        FN_MOVE_CARET,
        static_cast<DWORD>(targetRow),
        static_cast<DWORD>(targetColumn));
    int landedRow = -1;
    int landedColumn = -1;
    QueryCaret(landedRow, landedColumn);
    const bool landed = landedRow == targetRow;
    DesignerLog::Write(
        "UI_EVENT fast_jump sub=\"" + WideToUtf8(AnsiToWide(subNameAnsi)) +
        "\" target_row=" + std::to_string(targetRow) +
        " landed_row=" + std::to_string(landedRow) +
        " ok=" + std::to_string(landed ? 1 : 0));
    return landed;
}

// knownPageTextUtf8 is any render of this page the caller still holds. Lines
// inserted since then only shift the target, which the hint probe absorbs.
bool JumpToSubroutine(
    const std::string& subNameAnsi,
    int hintRow = -1,
    const std::string& knownPageTextUtf8 = std::string())
{
    if (ResolveRowFromProgramTree(subNameAnsi) >= 0) return true;
    int targetRow = -1;
    int targetColumn = 0;
    const PageScanResult cell = ScanPageForType(VT_SUB_NAME, subNameAnsi, false);
    if (cell.matchRow >= 0) {
        targetRow = cell.matchRow;
        targetColumn = cell.matchColumn;
    }
    if (targetRow < 0 && hintRow >= 0) {
        InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(hintRow), 0);
        PumpMessagesFor(60);
        const int first = hintRow > kJumpHintReach ? hintRow - kJumpHintReach : 0;
        const int last = hintRow + kJumpHintReach;
        for (int row = first; row <= last && row < kMaximumRowsToScan && targetRow < 0; ++row) {
            for (int column = 0; column < kMaximumColumnsToScan; ++column) {
                CellText probe;
                if (!ReadCell(row, column, probe) || !CellHasData(probe)) {
                    continue;
                }
                if (probe.type != VT_SUB_NAME || probe.text.empty()) {
                    continue;
                }
                if (_stricmp(probe.text.c_str(), subNameAnsi.c_str()) == 0) {
                    targetRow = row;
                    targetColumn = column;
                    break;
                }
            }
        }
    }
    if (targetRow < 0) {
        // Ask the page text where the block is before walking the page to find
        // out. Both preceding attempts read the grid, which stops at the fold;
        // the text does not, and its line index aims the caret straight at the
        // target instead of dragging a window across everything above it.
        std::string pageText = knownPageTextUtf8;
        if (pageText.empty()) {
            ReadPageCodeUtf8(pageText);
        }
        const int row = ResolveRowFromPageText(
            pageText, subNameAnsi, WideToUtf8(AnsiToWide(subNameAnsi)));
        if (row >= 0) {
            targetRow = row;
            targetColumn = 0;
        }
    }
    if (targetRow < 0) {
        std::vector<int> rows;
        int nameCells = 0;
        int lastRow = -1;
        int documentLastRow = -1;
        CollectSubroutineRowsWindowed(
            subNameAnsi, rows, nameCells, lastRow, documentLastRow);
        if (!rows.empty()) {
            targetRow = rows.front();
            targetColumn = 0;
        }
    }
    if (targetRow < 0) {
        DesignerLog::Write(
            "UI_EVENT jump_locate=0 sub=\"" + WideToUtf8(AnsiToWide(subNameAnsi)) +
            "\" hint_row=" + std::to_string(hintRow));
        return false;
    }
    InvokeIde(
        FN_MOVE_CARET,
        static_cast<DWORD>(targetRow),
        static_cast<DWORD>(targetColumn));
    int landedRow = -1;
    int landedColumn = -1;
    QueryCaret(landedRow, landedColumn);
    const bool landed = landedRow == targetRow;
    DesignerLog::Write(
        "UI_EVENT jump sub=\"" + WideToUtf8(AnsiToWide(subNameAnsi)) +
        "\" hint_row=" + std::to_string(hintRow) +
        " target_row=" + std::to_string(targetRow) +
        " landed_row=" + std::to_string(landedRow) +
        " ok=" + std::to_string(landed ? 1 : 0));
    return landed;
}

// Locates (or creates) the assembly page and the subroutine inside it. This
// is the shared backbone for normal events and Jade call statements.
PageEnsureResult EnsureAssemblySubPage(
    HWND mainWindow,
    HWND mdiClient,
    const std::wstring& assembly,
    const std::wstring& subName,
    const std::string& initialSource)
{
    PageEnsureResult out;
    const std::string subAnsi = WideToAnsi(subName);
    const std::string asmAnsi = WideToAnsi(assembly);
    const std::string subUtf8 = WideToUtf8(subName);
    const std::string asmUtf8 = WideToUtf8(assembly);
    if (subAnsi.empty() || asmAnsi.empty()) {
        out.message = "名称包含无法表示的字符";
        return out;
    }

    const auto found = LookupAssemblyPage(mainWindow, mdiClient, assembly);
    if (found.state != ProjectAssembly::State::Absent &&
        (found.state != ProjectAssembly::State::Found || !found.document)) {
        out.message = AssemblyLookupFailure(found, assembly);
        return out;
    }
    HWND targetDocument = found.document;
    if (found.state == ProjectAssembly::State::Absent) {
        HWND nativeDocument = FindAnyNativeMdiDocument(mdiClient);
        if (!ActivateDocument(mdiClient, nativeDocument)) {
            out.message = "没有可用于创建程序集的原生代码页";
            return out;
        }
        const std::vector<HWND> knownChildren = SnapshotMdiChildren(mdiClient);
        RunNativeMenuCommand(mainWindow, kMenuInsertAssembly, "insert_assembly");
        targetDocument = WaitForNewMdiDocument(mdiClient, knownChildren);
        if (targetDocument == nullptr) {
            out.message = "主菜单已执行“插入程序集”，但没有出现新的代码页";
            return out;
        }
        const bool activatedNow = ActivateDocument(mdiClient, targetDocument);
        PumpMessagesFor(150);
        DesignerLog::Write(
            "UI_EVENT new_document_activated hwnd=" +
            DesignerLog::HexPointer(targetDocument) +
            " activated=" + std::to_string(activatedNow ? 1 : 0) +
            " target=\"" + asmUtf8 + "\"");
        out.createdAssembly = true;
    }
    else if (!ActivateDocument(mdiClient, targetDocument)) {
        out.message = "找到目标程序集，但无法激活其代码页";
        return out;
    }
    // Rename before looking up source text, so every read verifies its owner.
    if (out.createdAssembly) {
        if (!RenameAssemblyViaApi(targetDocument, asmAnsi)) {
            out.message =
                "通过内部接口重命名为 " + asmUtf8 + " 未通过回读/标题验证，已按规则停止";
            return out;
        }
        DesignerLog::Write(
            "UI_EVENT assembly_renamed name=\"" + asmUtf8 +
            "\" title=\"" + WideToUtf8(GetWindowTitle(targetDocument)) + "\"");
    }
    std::string pageText;
    if (!ReadPageCodeUtf8(pageText) || !ParsePageCode(pageText).valid ||
        !ProjectAssembly::MatchesDocumentTitle(
            L"程序集: " + Utf8ToWide(ParsePageCode(pageText).assemblyUtf8), assembly)) {
        out.message = "目标程序集代码页读取失败或名称不一致，已停止写入";
        return out;
    }
    const auto sub = LookupSubroutine(mdiClient, targetDocument, subAnsi, pageText);
    if (sub.duplicateRows > 1 || sub.state == SubLookupState::Unreadable) {
        out.message = "子程序存在同名冲突或无法读取，已停止写入：" + subUtf8;
        return out;
    }
    if (sub.state == SubLookupState::ConfirmedAbsent) {
        if (!initialSource.empty()) {
            InvokeIde(FN_MOVE_BOTTOM, 0, 0);
            std::string error;
            const bool inserted = HookBridge::InsertAnsi(initialSource, error);
            PumpMessagesFor(180);
            const auto written = ConfirmMemoryWrite(subAnsi);
            DesignerLog::Write("HYBRID memory_shared_callback success=" +
                std::to_string(inserted ? 1 : 0) + " confirmed=" +
                std::to_string(written.present ? 1 : 0) + " row=" + std::to_string(written.row));
            if (!written.pageReadable || !written.present) {
                out.message = "目标程序集内未确认回调代码已写入，已停止后续写入：" + error;
                return out;
            }
            out.subRow = written.row;
        }
        else {
            if (!CreateSubroutineAtPage(subAnsi)) {
                out.message = "原生子程序创建未确认，已停止后续写入";
                return out;
            }
            out.subRow = ConfirmMemoryWrite(subAnsi).row;
        }
        out.createdSubroutine = true;
    }
    else {
        out.subRow = sub.row;
    }
    if (out.subRow < 0) {
        out.message = "子程序已存在，但无法定位编辑行，已停止写入：" + subUtf8;
        return out;
    }
    out.document = targetDocument;
    out.ok = true;
    return out;
}

} // namespace

namespace IdeEventRouter {

std::string DecodeWireField(std::wstring_view value)
{
    return PercentDecodeUtf8(value);
}

bool TryParseWebMessage(std::wstring_view wireMessage, UiEvent& event)
{
    event = {};
    const std::vector<std::wstring_view> parts = SplitTabs(wireMessage);
    if (parts.size() < 8 || parts[0] != L"JADE_EVT") {
        return false;
    }
    event.domEvent = PercentDecodeUtf8(parts[1]);
    event.controlType = PercentDecodeUtf8(parts[2]);
    event.elementId = PercentDecodeUtf8(parts[3]);
    event.value = PercentDecodeUtf8(parts[4]);
    event.checked = parts[5] == L"1";
    event.handlerName = PercentDecodeUtf8(parts[6]);
    event.assemblyName = PercentDecodeUtf8(parts[7]);
    if (parts.size() >= 10) {
        event.callType = PercentDecodeUtf8(parts[8]);
        event.callParam = PercentDecodeUtf8(parts[9]);
    }
    // A well-formed JADE_EVT is accepted even when it carries neither a handler
    // name nor a channel. Rejecting it here made the click vanish without a
    // word; Route turns it into an explanation the user can act on instead.
    return true;
}

RouteResult Route(HWND mainWindow, HWND mdiClient, const UiEvent& event)
{
    if (mainWindow == nullptr || mdiClient == nullptr ||
        !IsWindow(mainWindow) || !IsWindow(mdiClient)) {
        return Fail("invalid_context", "易语言编辑窗口尚未准备好");
    }
    if (IsWindowControlEvent(event)) {
        DesignerLog::Write(
            "UI_EVENT ignored window_control control=" + event.controlType +
            " id=\"" + event.elementId + "\" handler=\"" + event.handlerName +
            "\" param=\"" + event.callParam + "\"");
        return {true, "ignored_window_control", "已忽略窗口/弹窗控制按钮"};
    }
    if (g_routingInProgress.exchange(true)) {
        return Fail("busy", "上一个事件仍在处理中，请稍候再操作");
    }
    RoutingGuard routingGuard;
    ResetSessionCachesIfProjectChanged(mainWindow);

    // Nothing here identifies a callback: no data-jade-handler, no id/name/
    // title/aria-label to build a stable name from, no inline handler function,
    // and no channel. Inventing a name from the button's text would rename the
    // subroutine every time that text changed, so say what is missing instead
    // of writing a subroutine the user cannot keep.
    if (event.handlerName.empty() && event.callType.empty()) {
        DesignerLog::Write(
            "UI_EVENT no_handler_name dom=" + event.domEvent +
            " control=" + event.controlType + " id=\"" + event.elementId + "\"");
        return Fail(
            "no_handler_name",
            "该控件（" + event.elementId +
                "）没有 id/name/title，也没有 onclick 函数或 data-jade-channel，"
                "无法生成稳定的子程序名；请给它加上 id 或 data-jade-handler");
    }

    std::wstring handler = Utf8ToWide(event.handlerName);
    if (event.callType == "JadeView.通讯.订阅" && handler.empty() && !event.callParam.empty()) {
        handler = DeriveIpcNameFromChannel(Utf8ToWide(event.callParam));
    }
    // Older injected pages and hand-written markup may provide only an element
    // id. Common controls still have a stable identity (for example
    // btnInfoOk), so derive the conventional callback name instead of silently
    // rejecting the click.
    if (handler.empty() && IsCommonInteractiveControl(event) && !event.elementId.empty()) {
        handler = Utf8ToWide(event.elementId) + DefaultHandlerSuffix(event);
        DesignerLog::Write(
            "UI_EVENT derived_handler id=\"" + event.elementId +
            "\" handler=\"" + WideToUtf8(handler) + "\"");
    }
    handler = SanitizeIdentifier(handler, L"Jade事件");
    const std::string handlerUtf8 = WideToUtf8(handler);
    const std::string handlerAnsi = WideToAnsi(handler);
    if (handlerAnsi.empty()) {
        return Fail("invalid_name", "处理名包含当前易语言代码页无法表示的字符");
    }
    DesignerLog::Write(
        "UI_EVENT received dom=" + event.domEvent +
        " control=" + event.controlType +
        " id=\"" + event.elementId + "\" handler=\"" + handlerUtf8 +
        "\" call=\"" + event.callType + "\" param=\"" + event.callParam +
        "\" value=\"" + event.value +
        "\" checked=" + std::to_string(event.checked ? 1 : 0));

    // Branch A (explicit JadeView.通讯.订阅 elements) is covered by Branch C:
    // every page control event is wired through 通讯.订阅 with the channel
    // taken from data-jade-channel / a captured jade.invoke / the element id.

    // ---- Branch B: JadeView.App.注册事件 native lifecycle registration ----
    if (event.callType == "JadeView.App.注册事件") {
        const std::wstring eventNameWide = SanitizeIdentifier(
            Utf8ToWide(event.callParam), L"应用准备就绪");
        const std::string eventNameAnsi = WideToAnsi(eventNameWide);
        if (eventNameAnsi.empty()) {
            return Fail("register", "事件名为空或无法表示，请在 data-jade-event 中填写");
        }
        // Callback subroutine: UI_/ipc_ names go to UI_JadeView; all other
        // 注册事件 callbacks live in the subscription assembly.
        const std::wstring callbackAssembly =
            StartsWithInsensitive(handler, L"ipc_") || StartsWithInsensitive(handler, L"UI_")
                ? std::wstring(kSharedAssembly)
                : std::wstring(kSubscribeAssembly);
        const PageEnsureResult callbackPage = EnsureAssemblySubPage(
            mainWindow, mdiClient, callbackAssembly, handler);
        if (!callbackPage.ok) {
            return Fail("register", "回调子程序准备失败：" + callbackPage.message);
        }
        // 注册事件回调同样按生产签名补参数/函数体（app-ready 自带 创建主窗口
        // 分支，window-created 自带 返回(0) 等）。
        EnsureLifecycleCallbackBody(
            mainWindow,
            mdiClient,
            callbackPage.document,
            eventNameAnsi,
            handler,
            callbackPage.subRow);

        const std::string registerSubAnsi = WideToAnsi(kRegisterSub);
        const std::string statementText =
            WideToAnsi(L"JadeView.App.注册事件 (\"") + eventNameAnsi +
            WideToAnsi(L"\", &") + handlerAnsi + ")\r\n";
        const std::string detectToken =
            WideToAnsi(L"JadeView.App.注册事件(\"") + eventNameAnsi + "\"";
        const PageEnsureResult hookPage = EnsureAssemblySubPage(
            mainWindow, mdiClient, std::wstring(kRegisterAssembly), std::wstring(kRegisterSub));
        if (!hookPage.ok) {
            return Fail("register", "固定子程序准备失败：" + hookPage.message);
        }
        // 注册行必须位于初始化块之前；骨架已存在时插到初始化行前面。
        const bool appended = AppendStatementsIfMissing(
            mdiClient,
            hookPage.document,
            registerSubAnsi,
            statementText,
            detectToken,
            hookPage.subRow,
            WideToAnsi(L"JadeView.App.初始化"));
        const bool initReady = EnsureJadeViewStartupSkeleton(
            mainWindow, mdiClient, hookPage.document, registerSubAnsi, hookPage.subRow);
        const bool jumped = JumpToSubroutine(registerSubAnsi, hookPage.subRow);
        DesignerLog::Write(
            "UI_EVENT register hook_appended=" + std::to_string(appended ? 1 : 0) +
            " init_ready=" + std::to_string(initReady ? 1 : 0) +
            " jump=" + std::to_string(jumped ? 1 : 0) +
            " 注意：若编译提示未知的 JadeView 命令，请确认已导入 JadeView.ec 模块（其依赖 spec 支持库需在支持库配置中启用）");
        return {
            true,
            "register",
            "已处理事件注册（" + WideToUtf8(handler) + " ← " +
                WideToUtf8(eventNameWide) + "）"};
    }

    // ---- Branch C: normal page control event (被单击 / 选择项被改变 / ...) ----
    // Every page control event is wired through JadeView.通讯.订阅: the
    // channel is the captured jade.invoke channel, falling back to the
    // element id. Callback subroutines live in Jade_通讯_订阅集 (UI_JadeView
    // for ipc_/UI_ names), matching the subscription assembly.
    std::wstring assembly;
    if (!event.assemblyName.empty()) {
        assembly = SanitizeIdentifier(Utf8ToWide(event.assemblyName), kSubscribeAssembly);
    }
    if (assembly.empty()) {
        assembly.assign(
            StartsWithInsensitive(handler, L"ipc_") || StartsWithInsensitive(handler, L"UI_")
                ? std::wstring(kSharedAssembly)
                : std::wstring(kSubscribeAssembly));
    }
    const std::string assemblyAnsi = WideToAnsi(assembly);
    const std::string assemblyUtf8 = WideToUtf8(assembly);
    if (assemblyAnsi.empty()) {
        return Fail("invalid_name", "程序集名包含当前易语言代码页无法表示的字符");
    }

    std::wstring channelWide = Utf8ToWide(event.callParam);
    if (channelWide.empty()) {
        channelWide = L"ui:" + Utf8ToWide(event.elementId);
    }
    const std::string channelAnsi = WideToAnsi(channelWide);
    const std::string channelUtf8 = WideToUtf8(channelWide);
    if (channelAnsi.empty()) {
        return Fail("subscribe", "订阅通道名为空或无法表示");
    }

    // This branch is the hybrid memory-write path.  WebView2 has already queued
    // the event onto the preview window message queue before Route is entered,
    // so the private jadehook editor call is no longer made from inside the
    // WebView callback.  Do not silently replace this with the public IDE path:
    // the jade-hybrid branch is specifically the memory bridge version.
    DesignerLog::Write("HYBRID memory_bridge_path assembly=\"" + assemblyUtf8 + "\"");

    // Every write below goes through the hook, and the hook refuses any host but
    // the e5.95 build it was compiled against - generate, insert and page read
    // alike, all reporting "generate_invalid_arguments". There is no fallback to
    // fail over to, so stop here and name the host: the old message sent the
    // reader after jadehook.dll, which was never the problem.
    const HookBridge::HostInfo host = HookBridge::InspectHost();
    if (!host.ok()) {
        DesignerLog::Write(
            "HYBRID host_unsupported host=\"" + host.exeNameUtf8 +
            "\" name_ok=" + std::to_string(host.nameSupported ? 1 : 0) +
            " build_ok=" + std::to_string(host.buildSupported ? 1 : 0));
        return Fail(
            "subscribe",
            host.nameSupported
                ? "当前 e5.95.exe 与内存桥接不是配套版本（编辑器入口特征不符），"
                  "请换回与 jadehook.dll 配套的 e5.95.exe"
                : "内存桥接只能在 e5.95.exe 中工作，当前 IDE 进程是 " +
                      host.exeNameUtf8 +
                      "；请用 e5.95.exe 打开本项目（双击 .e 文件会启动 e.exe）");
    }

    // The 0908 variant creates/repairs directly in the project model before
    // touching MDI documents. A complete event continues to the existing jump
    // path; a failure must never fall back to activate-then-create.
    const std::string backgroundStatement =
        WideToAnsi(L"JadeView.通讯.订阅 (\"") + channelAnsi + WideToAnsi(L"\", &") + handlerAnsi + ")\r\n";
    const std::string backgroundCallback =
        WideToAnsi(L".子程序 ") + handlerAnsi + WideToAnsi(L", 整数型\r\n") +
        WideToAnsi(L".参数 WinId, 整数型\r\n.参数 msg, 文本型\r\n\r\n") +
        WideToAnsi(L"msg ＝ UTF8文本到GBK文本 (msg)\r\n返回 (JadeView.文本.创建指针 (\"ok\"))\r\n");
    std::string backgroundError;
    HookBridge::BackgroundChange backgroundChange = HookBridge::BackgroundChange::Unknown;
    const int background = HookBridge::EnsureBackground(assemblyAnsi, WideToAnsi(kSubscribeAssembly),
        WideToAnsi(kSubscribeSub), handlerAnsi, backgroundStatement, backgroundCallback, backgroundError,
        backgroundChange);
    if (background == 1) {
        const wchar_t* suffix = backgroundChange == HookBridge::BackgroundChange::CallbackCreated
            ? L" 子程序已创建"
            : backgroundChange == HookBridge::BackgroundChange::SubscriptionRepaired
                ? L" 订阅已补齐" : L" 子程序已就绪";
        return {true, "create_background", "已在后台创建或补齐 " + assemblyUtf8 + " → " + handlerUtf8 + "，已确认订阅，保持网页预览",
            handler + suffix};
    }
    if (background != 2) {
        return Fail("create_background", "后台创建未完成，未改用跳转创建：" + backgroundError);
    }

    // A closed code page is not evidence that its assembly was deleted.
    auto subscriptionAssembly = LookupAssemblyPage(mainWindow, mdiClient, kSubscribeAssembly);
    if (subscriptionAssembly.state != ProjectAssembly::State::Absent &&
        (subscriptionAssembly.state != ProjectAssembly::State::Found ||
         !subscriptionAssembly.document)) {
        return Fail("subscribe", AssemblyLookupFailure(subscriptionAssembly, kSubscribeAssembly));
    }
    bool memoryAssemblyCreated = false;
    if (subscriptionAssembly.state == ProjectAssembly::State::Absent) {
        std::string hookError;
        memoryAssemblyCreated = HookBridge::GenerateAssembly(
            WideToAnsi(kSubscribeAssembly), channelAnsi, handlerAnsi, hookError);
        DesignerLog::Write(
            "HYBRID memory_generate success=" + std::to_string(memoryAssemblyCreated ? 1 : 0) +
            " error=\"" + hookError + "\"");
        PumpMessagesFor(250);
        subscriptionAssembly = LookupAssemblyPage(mainWindow, mdiClient, kSubscribeAssembly);
        if (subscriptionAssembly.state != ProjectAssembly::State::Found ||
            !subscriptionAssembly.document) {
            return Fail("subscribe", "创建后未确认唯一的订阅程序集，已停止后续写入：" +
                hookError + " " + subscriptionAssembly.reason);
        }
    }

    const std::string fastJumpKey =
        assemblyAnsi + "\x1f" + handlerAnsi + "\x1f" + channelAnsi;
    if (assembly == kSubscribeAssembly &&
        g_fastJumpKeys.find(fastJumpKey) != g_fastJumpKeys.end()) {
        HWND existingPage = FindMdiDocument(mdiClient, assembly);
        std::string currentPage;
        if (existingPage != nullptr && ActivateDocument(mdiClient, existingPage) &&
            ReadPageCodeUtf8(currentPage) &&
            CountSubInPage(ParsePageCode(currentPage), handlerUtf8) == 1 &&
            CountSubInPage(ParsePageCode(currentPage), WideToUtf8(kSubscribeSub)) == 1 &&
            PageTextHasSubscription(currentPage, WideToUtf8(kSubscribeSub), channelUtf8, handlerUtf8)) {
            PageScanResult located = ScanPageForType(VT_SUB_NAME, handlerAnsi, false);
            if (located.matchRow < 0) {
                // The grid knows only the rows near the caret, so a handler that
                // was written earlier and then left behind by later clicks reads
                // as absent here. Falling through on that answer sent a page whose
                // code was already complete into the full create/repair path,
                // where two lookups walked the caret over the whole document
                // before concluding the same. One page render settles it.
                const int row = ResolveRowFromPageText(currentPage, handlerAnsi, handlerUtf8);
                if (row >= 0) {
                    located.matchRow = row;
                    located.matchColumn = 0;
                }
            }
            if (located.matchRow >= 0 &&
                JumpToLocatedSubroutine(handlerAnsi, located.matchRow, located.matchColumn)) {
                DesignerLog::Write(
                    "UI_EVENT routed action=fast_jump handler=\"" + handlerUtf8 +
                    "\" assembly=\"" + assemblyUtf8 +
                    "\" channel=\"" + channelUtf8 + "\"");
                return {
                    true,
                    "jump",
                    "jumped to " + assemblyUtf8 + " -> " + handlerUtf8};
            }
        }
        // A stale session key must not hide a missing callback. Fall through
        // once to the existing repair/create path, then relearn the key.
        g_fastJumpKeys.erase(fastJumpKey);
        DesignerLog::Write(
            "UI_EVENT fast_jump fallback handler=\"" + handlerUtf8 +
            "\" assembly=\"" + assemblyUtf8 + "\"");
    }

    // The subscribe assembly is the hybrid bridge's own page. Do not call the
    // public FN_INSERT_NEW_SUB path here: it was the source of the
    // "native_only" log and made this version look like the normal build. Create
    // or repair the fixed routine through the private text paste bridge instead.
    PageEnsureResult hookPage;
    hookPage.document = FindMdiDocument(mdiClient, std::wstring(kSubscribeAssembly));
    hookPage.createdAssembly = memoryAssemblyCreated;
    if (hookPage.document == nullptr) {
        return Fail(
            "subscribe",
            memoryAssemblyCreated
                ? "内存桥接报告程序集已创建，但其代码页未出现"
                : "未找到通讯订阅程序集代码页，且内存桥接创建失败（请检查 jadehook.dll）");
    }
    if (!ActivateDocument(mdiClient, hookPage.document)) {
        return Fail("subscribe", "无法激活通讯订阅程序集代码页");
    }
    const std::string subscribeSubAnsi = WideToAnsi(kSubscribeSub);
    // Source text of the subscription assembly page, rendered once for this
    // event. It answers both "is the fixed routine already here" and "is this
    // subscription line already here", and it is the only view of the page that
    // does not stop at the rows the grid has materialised.
    std::string subscribePageTextUtf8;
    if (!ReadPageCodeUtf8(subscribePageTextUtf8)) {
        return Fail("subscribe", "订阅程序集代码页读取失败，已停止写入");
    }
    const PageCodeInfo subscriptionPage = ParsePageCode(subscribePageTextUtf8);
    if (!subscriptionPage.valid ||
        Utf8ToWide(subscriptionPage.assemblyUtf8) != kSubscribeAssembly) {
        return Fail("subscribe", "活动代码页不是目标订阅程序集，已停止写入");
    }
    if (CountSubInPage(subscriptionPage, WideToUtf8(kSubscribeSub)) > 1 ||
        (assembly == kSubscribeAssembly && CountSubInPage(subscriptionPage, handlerUtf8) > 1)) {
        return Fail("subscribe", "订阅程序集内存在同名子程序，已停止写入，请先检查重复项");
    }
    PageScanResult fixedSub = ScanPageForType(VT_SUB_NAME, subscribeSubAnsi, false);
    bool fixedSubExists = fixedSub.matchRow >= 0;
    if (!fixedSubExists) {
        // The fixed routine sits in the first rows of the page, and those rows
        // read back empty while the editor window is parked further down. Taking
        // that for absence appends a second .子程序 Jade_通讯_订阅, after which
        // every later click has two candidate homes for its subscription line
        // and picks whichever one it happens to see. A row the grid returns is
        // proof; a missing row only means ask the page source.
        const SubLookupResult fixedLookup = LookupSubroutine(
            mdiClient, hookPage.document, subscribeSubAnsi, subscribePageTextUtf8);
        if (subscribePageTextUtf8.empty()) {
            subscribePageTextUtf8 = fixedLookup.pageTextUtf8;
        }
        if (fixedLookup.state == SubLookupState::Unreadable) {
            return Fail(
                "subscribe",
                "代码页读取失败，已放弃写入以免重复插入固定子程序（请重试或检查 jadehook.dll）");
        }
        // FoundTextOnly counts as existing even though it carries no row: the
        // subscription write below re-locates the row for itself, while creating
        // again could never be undone.
        fixedSubExists = fixedLookup.state != SubLookupState::ConfirmedAbsent;
        fixedSub.matchRow = fixedLookup.row;
    }
    // Writing it is deferred until the callback below has also been decided, so
    // that both missing blocks travel in one package. Creating the fixed routine
    // on its own leaves the page ending in a subroutine that has neither
    // parameters nor a body, and a .子程序 paste into that tail is accepted by
    // the paste dispatcher yet never appears on the page - which is how a click
    // could finish with nothing but the assembly and an empty Jade_通讯_订阅.
    const std::string fixedSubText =
        fixedSubExists
            ? std::string()
            : WideToAnsi(L".子程序 ") + subscribeSubAnsi + WideToAnsi(L"\r\n");
    // For callbacks in the subscription assembly, use the memory bridge to
    // insert the complete callback block.  The bridge is called only after the
    // queued UI event reaches the native window procedure, not from the
    // WebView2 callback itself.  Never follow a successful insert with a public
    // create attempt merely because the index has not caught up yet.
    PageEnsureResult result;
    // Source text of the page the callback itself lives on. Same page as
    // subscribePageTextUtf8 for callbacks in the subscription assembly, a
    // different one for the shared-assembly path below, so the two must not be
    // conflated: the callback body would then be checked against a page that
    // never contained it.
    std::string pageTextUtf8;
    // Set only when the hook pasted the whole .子程序/.参数/函数体 block during
    // this event. That text went through the IDE's own parser, so the body is
    // complete by construction and must not be re-derived from a page read.
    bool hookWroteFullBlock = false;
    const bool callbackInSubscribeAssembly =
        _wcsicmp(assembly.c_str(), kSubscribeAssembly) == 0;
    // A plain grid scan reports a callback the IDE has not materialised as
    // absent, and pasting on that answer appends a second copy of the whole
    // block. Ask the page source instead, and treat "cannot read" as its own
    // answer rather than as absence.
    SubLookupResult callbackLookup;
    if (callbackInSubscribeAssembly) {
        callbackLookup = LookupSubroutine(
            mdiClient, hookPage.document, handlerAnsi, subscribePageTextUtf8);
        pageTextUtf8 = callbackLookup.pageTextUtf8;
        // Same page here, so a render the callback lookup had to do covers the
        // subscription check below as well.
        subscribePageTextUtf8 = pageTextUtf8;
        if (callbackLookup.state == SubLookupState::Unreadable) {
            return Fail(
                "subscribe",
                "代码页读取失败，已放弃写入以免重复插入回调（请重试或检查 jadehook.dll）");
        }
    }
    const bool callbackExists =
        callbackLookup.state == SubLookupState::Found ||
        callbackLookup.state == SubLookupState::FoundTextOnly;
    const std::string fullCallbackText =
                  WideToAnsi(L".子程序 ") + handlerAnsi + WideToAnsi(L", 整数型\r\n") +
                  WideToAnsi(L".参数 WinId, 整数型\r\n") +
                  WideToAnsi(L".参数 msg, 文本型\r\n\r\n") +
                  WideToAnsi(L"msg ＝ UTF8文本到GBK文本 (msg)\r\n") +
                  WideToAnsi(L"返回 (JadeView.文本.创建指针 (\"ok\"))\r\n");
    const std::string callbackText =
        (!callbackInSubscribeAssembly || callbackExists) ? std::string() : fullCallbackText;
    // Wire the control into Jade_通讯_订阅集.Jade_通讯_订阅.
    const std::string statementText =
        WideToAnsi(L"JadeView.通讯.订阅 (\"") + channelAnsi +
        WideToAnsi(L"\", &") + handlerAnsi + ")\r\n";
    // Whatever the subscription page is missing goes in as one package, pasted
    // at the tail of a page that still ends in .程序集 or in a subroutine body -
    // never at the tail of the empty subroutine a two-step write would have just
    // created there.
    // The subscription line rides along whenever the fixed routine is created
    // here, because the caret append below needs a statement row to aim at and a
    // body that was just created has none: the paste is accepted and the line
    // never appears. As source text the IDE's own parser places it correctly.
    const std::string pendingText =
        fixedSubText.empty() ? callbackText
                             : fixedSubText + statementText + callbackText;
    if (!pendingText.empty()) {
        ActivateDocument(mdiClient, hookPage.document);
        InvokeIde(FN_MOVE_BOTTOM, 0, 0);
        std::string hookError;
        const bool inserted = HookBridge::InsertAnsi(pendingText, hookError);
        PumpMessagesFor(180);
        // One render of the page answers for both blocks, so a click that has to
        // build the assembly from scratch still reads the page only once here.
        std::string writtenPageTextUtf8;
        if (!fixedSubText.empty()) {
            const MemoryWriteCheck fixedWritten = ConfirmMemoryWrite(subscribeSubAnsi);
            writtenPageTextUtf8 = fixedWritten.pageTextUtf8;
            DesignerLog::Write(
                "HYBRID memory_fixed_sub success=" + std::to_string(inserted ? 1 : 0) +
                " confirmed=" + std::to_string(fixedWritten.present ? 1 : 0) +
                " row=" + std::to_string(fixedWritten.row) +
                " error=\"" + hookError + "\"");
            if (!inserted || !fixedWritten.present) {
                return Fail("subscribe", hookError.empty()
                    ? "内存桥接未确认固定子程序已写入"
                    : "固定子程序内存写入失败：" + hookError);
            }
            fixedSub.matchRow = fixedWritten.row;
            hookPage.createdSubroutine = true;
        }
        if (!callbackText.empty()) {
            const MemoryWriteCheck callbackWritten =
                ConfirmMemoryWrite(handlerAnsi, writtenPageTextUtf8);
            writtenPageTextUtf8 = callbackWritten.pageTextUtf8;
            // A TRUE return only means the private paste dispatcher ran. The
            // write is accepted only once the page's own text carries the named
            // subroutine; otherwise the text was rejected or decoded incorrectly,
            // and reporting it as written would leave a callback with no
            // parameters and no body behind.
            result.ok = inserted && callbackWritten.present;
            result.subRow = callbackWritten.row;
            result.createdSubroutine = inserted;
            result.message = (inserted && !callbackWritten.present && hookError.empty())
                ? std::string(
                      "回调子程序粘贴后未出现在代码页，已放弃写入参数与代码（请重试）")
                : hookError;
            hookWroteFullBlock = result.ok;
            DesignerLog::Write(
                "HYBRID memory_callback success=" + std::to_string(inserted ? 1 : 0) +
                " confirmed=" + std::to_string(result.ok ? 1 : 0) +
                " row=" + std::to_string(callbackWritten.row) +
                " error=\"" + hookError + "\"");
        }
        if (!writtenPageTextUtf8.empty()) {
            subscribePageTextUtf8 = writtenPageTextUtf8;
            // Only the same page may be handed on as the callback's own text.
            // For a callback in another assembly this render describes the
            // subscription page instead, and the body check below would then be
            // made against a page that never contained the callback.
            if (callbackInSubscribeAssembly) {
                pageTextUtf8 = writtenPageTextUtf8;
            }
        }
    }
    if (!callbackInSubscribeAssembly) {
        result = EnsureAssemblySubPage(mainWindow, mdiClient, assembly, handler, fullCallbackText);
        hookWroteFullBlock = result.ok && result.createdSubroutine;
    }
    else {
        result.document = hookPage.document;
        if (callbackText.empty()) {
            // Already there; the lookup above is the proof.
            result.ok = true;
            result.subRow = callbackLookup.row;
        }
    }
    // The fixed routine's row is the append point for the subscription line
    // below, and the only fallback when the scan there comes back empty.
    hookPage.subRow = fixedSub.matchRow;
    hookPage.ok = hookPage.subRow >= 0;
    if (!result.ok) {
        return Fail(result.createdAssembly ? "create_assembly" : "create_sub",
            result.message.empty() ? "memory bridge callback insertion failed" : result.message);
    }
    // An existing callback in another assembly needs its own source before any
    // signature/body repair. A failed read must stop this event, not just skip
    // body checks while still modifying parameters or the subscription page.
    if (pageTextUtf8.empty() && !hookWroteFullBlock) {
        if (result.document != nullptr && IsWindow(result.document)) {
            ActivateDocument(mdiClient, result.document);
        }
        if (!ReadPageCodeUtf8(pageTextUtf8) || !ParsePageCode(pageTextUtf8).valid) {
            return Fail("subscribe", "回调程序集代码页读取失败，已停止写入");
        }
    }
    // Give the callback its production signature (WinId/msg 参数、UTF-8 提醒、
    // 返回响应指针与整数型返回值) — mirrors ipc_获取二维码 in the reference
    // project; no-op when the signature already exists.
    SetupSubscribeCallbackBody(
        mainWindow, mdiClient, result.document, handler, result.subRow,
        hookWroteFullBlock, pageTextUtf8);

    const std::string detectToken =
        WideToAnsi(L"JadeView.通讯.订阅(\"") + channelAnsi + "\"";
    bool hookAppended = false;
    // The common case is a button that is already wired up. The page source
    // says so without touching the editor, while the grid path below has to
    // scroll to the top of the document and back - which is the caret wandering
    // the user sees before an ordinary jump lands. It is also the only source
    // that can see the fixed routine on a page taller than the editor, and the
    // grid answering "absent" there is what appended a second identical
    // subscription line.
    if (PageTextHasSubscription(
            subscribePageTextUtf8, WideToUtf8(kSubscribeSub), channelUtf8, handlerUtf8)) {
        DesignerLog::Write(
            "HYBRID subscription_present_from_page_text channel=\"" + channelUtf8 + "\"");
        hookAppended = true;
    }
    else {
        // Parameter page replacement can shift every following physical row.
        // Re-locate the fixed subscription routine immediately before writing
        // it. Scroll to the top first: the fixed routine sits in the first rows,
        // and on a page taller than the editor those rows are only readable
        // while the window is actually over them.
        if (!ActivateDocument(mdiClient, hookPage.document)) {
            return Fail("subscribe", "无法切回订阅程序集，已停止写入");
        }
        InvokeIde(FN_MOVE_TOP, 0, 0);
        PumpMessagesFor(60);
        PageEnsureResult currentHookPage = hookPage;
        const PageScanResult refreshedHook =
            ScanPageForType(VT_SUB_NAME, subscribeSubAnsi, false);
        if (refreshedHook.matchRow >= 0) {
            currentHookPage.subRow = refreshedHook.matchRow;
        }
        if (currentHookPage.subRow < 0) {
            // The routine exists - the page text said so - but neither source
            // will say which row it occupies, and row 0 is the assembly header,
            // not an append point.
            return Fail(
                "subscribe",
                "无法定位固定子程序所在行，已放弃写入订阅行以免插到错误位置（请重试）");
        }
        const StatementArea hookArea = ScanSubStatementArea(currentHookPage.subRow);
        bool hookAlreadyPresent = false;
        bool hookMatchKept = false;
        for (const auto& entry : hookArea.lines) {
            if (IsSubscriptionStatement(entry.second, channelAnsi, handlerAnsi)) {
                if (hookMatchKept) {
                    RenameCellAt(entry.first, 0, std::string());
                    DesignerLog::Write(
                        "HYBRID subscription_duplicate_removed row=" +
                        std::to_string(entry.first));
                }
                else {
                    hookAlreadyPresent = true;
                    hookMatchKept = true;
                }
            }
        }
        if (!hookAlreadyPresent) {
            ActivateDocument(mdiClient, currentHookPage.document);
            // 注册事件 has always written its line through this helper, and a
            // subscription line is the same kind of write. The hand-rolled
            // append that used to stand here aimed the caret at
            // hookArea.lastRow - the body's trailing structural row, which
            // ScanSubStatementArea counts because some cell there carries data
            // even though its statement cell is empty. Both of that row's
            // consequences were missed: the "open a fresh row first" step was
            // skipped because the cell read back empty, and e5.95 clamps a
            // caret aimed at a blank statement row back onto the previous
            // content row. The paste landed on neither row and was dropped.
            // The helper targets that blank row on purpose, recovers from the
            // clamp with FN_MOVE_DOWN, and writes through the editor's own
            // parser instead of the paste dispatcher.
            const bool appended = AppendStatementsIfMissing(
                mdiClient,
                currentHookPage.document,
                subscribeSubAnsi,
                statementText,
                detectToken,
                currentHookPage.subRow);
            // The helper confirms against the grid, which e5.95 rebuilds
            // lazily, so its FALSE is not proof that nothing was written. The
            // page's own source is proof, and it is the same view the fast path
            // above trusts.
            PumpMessagesFor(120);
            bool subscriptionConfirmed = false;
            std::string verifyPageTextUtf8;
            if (ReadPageCodeUtf8(verifyPageTextUtf8)) {
                subscriptionConfirmed = PageTextHasSubscription(
                    verifyPageTextUtf8, WideToUtf8(kSubscribeSub), channelUtf8,
                    handlerUtf8);
                subscribePageTextUtf8 = verifyPageTextUtf8;
            }
            DesignerLog::Write(
                "HYBRID memory_subscription appended=" + std::to_string(appended ? 1 : 0) +
                " confirmed=" + std::to_string(subscriptionConfirmed ? 1 : 0) +
                " sub_row=" + std::to_string(currentHookPage.subRow) +
                " area_last_row=" + std::to_string(hookArea.lastRow) +
                " area_lines=" + std::to_string(static_cast<int>(hookArea.lines.size())));
            if (!subscriptionConfirmed) {
                return Fail(
                    "subscribe",
                    "订阅行未出现在 Jade_通讯_订阅 中（请重试）");
            }
            hookAppended = true;
        }
        else {
            hookAppended = true;
        }
    }

    if (!ActivateDocument(mdiClient, result.document)) {
        return Fail("jump", "订阅已处理，但无法切回回调程序集");
    }
    const bool jumped = JumpToSubroutine(
        handlerAnsi,
        result.subRow,
        callbackInSubscribeAssembly ? subscribePageTextUtf8 : pageTextUtf8);
    if (jumped || result.ok) {
        // The callback/subscription are now known to exist for this session;
        // future clicks can use the no-edit fast path above.
        g_fastJumpKeys.insert(fastJumpKey);
    }
    DesignerLog::Write(
        "UI_EVENT routed action=" + std::string(result.createdAssembly ? "create_assembly"
            : result.createdSubroutine ? "create_sub" : "jump") +
        " handler=\"" + handlerUtf8 +
        "\" assembly=\"" + assemblyUtf8 +
        "\" channel=\"" + channelUtf8 +
        "\" hook_appended=" + std::to_string(hookAppended ? 1 : 0) +
        " jump=" + std::to_string(jumped ? 1 : 0));

    std::string message;
    if (result.createdAssembly) {
        message = "已新建程序集 " + assemblyUtf8 + " → " + handlerUtf8;
    }
    else if (result.createdSubroutine) {
        message = "已在 " + assemblyUtf8 + " 中新建 " + handlerUtf8;
    }
    else {
        message = "已跳转到 " + assemblyUtf8 + " → " + handlerUtf8;
    }
    message += "；已同步通讯.订阅（" + channelUtf8 + "）" +
        (hookAppended ? "" : "（订阅行已存在）");
    if (!jumped) {
        message += "（代码已确认，未自动定位光标）";
    }
    return {true, result.createdAssembly || result.createdSubroutine ? "create" : "jump", message};
}

std::wstring BuildAckMessage(const RouteResult& result)
{
    std::wstring message = result.succeeded ? L"成功：" : L"未完成：";
    message += Utf8ToWide(result.message);
    return message;
}

} // namespace IdeEventRouter
