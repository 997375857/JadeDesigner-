#include "IdeEventRouter.h"

#include "DesignerLog.h"
#include "HookBridge.h"

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

constexpr wchar_t kDefaultAssembly[] = L"Jade通讯注册事件";
constexpr wchar_t kSharedAssembly[] = L"UI_JadeView";
constexpr wchar_t kSubscribeAssembly[] = L"Jade_通讯_订阅集";
constexpr wchar_t kSubscribeSub[] = L"Jade_通讯_订阅";
constexpr wchar_t kRegisterAssembly[] = L"UI_启动JadeView";
constexpr wchar_t kRegisterSub[] = L"UI_启动JadeView";
constexpr int kMaximumRowsToScan = 4096;
constexpr int kMaximumColumnsToScan = 4;
constexpr int kMaximumMissingRowsToStop = 64;

// Easy Language 5.95 native menu command identifier, verified from the
// target e5.95.exe menu resources (SHA256 368CBBD3...ABE1409).
constexpr UINT kMenuInsertAssembly = 32782; // 0x800E -> FN_INSERT_NEW_MOD
constexpr UINT kMenuSelectAll = 33009; // 0x80F1
constexpr UINT kMenuCopy = 57634; // 0xE122
constexpr UINT kMenuPaste = 57637; // 0xE125
constexpr DWORD kPasteSettleMs = 300;
constexpr DWORD kNativeCommandSettleMs = 35;
constexpr DWORD kInsertSettleMs = 250;

// All source-code edits go through the e5.95 public IDE function interface
// (NotifySys/NES_RUN_FUNC). Reverse-engineering of e5.95.exe (1.6.6 round)
// confirmed: the FN_GET_PRG_TEXT handler (0x004C4D64) always returns TRUE -
// even for out-of-range cells - filling type/isTitle but reporting
// reportedSize = strlen+1 only when a real cell was hit; the IDE's own
// name-cell commit path (0x0044D0B0) uses FN_SET_AND_COMPILE_PRG_ITEM_TEXT;
// the editor's '.' keyboard handler (0x004C2290) uses FN_INSERT_TEXT. No
// clipboard involvement anywhere.

struct KnownAssemblyDocument {
    std::wstring name;
    HWND document = nullptr;
};

std::vector<KnownAssemblyDocument> g_knownAssemblyDocuments;

std::atomic_bool g_routingInProgress{false};
std::set<std::string> g_writtenSubscriptionKeys;

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
        CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_ACP, 0, value.data(), static_cast<int>(value.size()),
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
        CP_ACP, WC_NO_BEST_FIT_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(required), '\0');
    BOOL usedDefault = FALSE;
    WideCharToMultiByte(
        CP_ACP, WC_NO_BEST_FIT_CHARS, value.data(), static_cast<int>(value.size()),
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

bool TitleContainsName(std::wstring_view title, std::wstring_view name)
{
    if (name.empty() || title.size() < name.size()) {
        return false;
    }
    for (size_t index = 0; index + name.size() <= title.size(); ++index) {
        if (_wcsnicmp(title.data() + index, name.data(), name.size()) == 0) {
            return true;
        }
    }
    return false;
}

HWND FindMdiDocument(HWND mdiClient, std::wstring_view pageName)
{
    for (auto iterator = g_knownAssemblyDocuments.begin();
         iterator != g_knownAssemblyDocuments.end();) {
        if (iterator->document == nullptr || !IsWindow(iterator->document) ||
            GetParent(iterator->document) != mdiClient) {
            iterator = g_knownAssemblyDocuments.erase(iterator);
            continue;
        }
        if (_wcsicmp(iterator->name.c_str(), std::wstring(pageName).c_str()) == 0) {
            return iterator->document;
        }
        ++iterator;
    }
    HWND child = GetWindow(mdiClient, GW_CHILD);
    while (child != nullptr) {
        if (GetParent(child) == mdiClient &&
            (GetWindowLongPtrW(child, GWL_EXSTYLE) & WS_EX_MDICHILD) != 0 &&
            TitleContainsName(GetWindowTitle(child), pageName)) {
            return child;
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
    return nullptr;
}

void RememberAssemblyDocument(std::wstring_view name, HWND document)
{
    if (document == nullptr || !IsWindow(document)) {
        return;
    }
    for (KnownAssemblyDocument& known : g_knownAssemblyDocuments) {
        if (_wcsicmp(known.name.c_str(), std::wstring(name).c_str()) == 0) {
            known.document = document;
            return;
        }
    }
    g_knownAssemblyDocuments.push_back({std::wstring(name), document});
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

// Inserts ".子程序 <name>" at the bottom of the active page.
//
// Path A: place the caret on the trailing empty row below the last data row
// (verified through FN_GET_CARET_ROW_INDEX, because NES_RUN_FUNC returns TRUE
// even when the move targeted a non-existent row) and insert the directive
// text through FN_INSERT_TEXT - the editor parses it exactly like typing.
//
// Path B (fresh assembly pages have no trailing row): the IDE's own
// FN_INSERT_NEW_SUB creates a sub template, whose name cell is then renamed
// via FN_SET_AND_COMPILE_PRG_ITEM_TEXT - the same commit path the IDE's
// name-cell editor uses.
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

bool CreateSubroutineAtPage(const std::string& subNameAnsi)
{
    const int subCountBefore = CountCellsOfType(VT_SUB_NAME);
    const PageScanResult anyCell = ScanPageForType(VT_SUB_NAME, std::string(), true);
    const int bottomRow = anyCell.lastRow;
    DesignerLog::Write(
        "UI_EVENT create_sub begin bottom_row=" + std::to_string(bottomRow) +
        " subs_before=" + std::to_string(subCountBefore));

    // Path A: trailing empty row + direct directive insert.
    if (bottomRow >= 0) {
        InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(bottomRow + 1), 0);
        int caretRow = -1;
        int caretColumn = -1;
        if (QueryCaret(caretRow, caretColumn) && caretRow == bottomRow + 1) {
            const std::string insertText = ".子程序 " + subNameAnsi + "\r\n";
            InvokeIde(
                FN_INSERT_TEXT,
                PointerToDword(const_cast<char*>(insertText.c_str())),
                FALSE);
            PumpMessagesFor(kInsertSettleMs);
            if (ScanPageForType(VT_SUB_NAME, subNameAnsi, false).matchRow >= 0) {
                DesignerLog::Write("UI_EVENT create_sub path=insert_text ok=1");
                return true;
            }
            DesignerLog::Write("UI_EVENT create_sub path=insert_text ok=0");
        }
        else {
            DesignerLog::Write(
                "UI_EVENT create_sub no_trailing_row caret=" +
                std::to_string(caretRow) + "/" + std::to_string(caretColumn));
        }
    }

    // Path B: IDE-native "insert new sub" + rename of its name cell.
    InvokeIde(FN_MOVE_BOTTOM, 0, 0);
    PumpMessagesFor(80);
    InvokeIde(FN_INSERT_NEW_SUB, 0, 0);
    PumpMessagesFor(kInsertSettleMs);
    const int subCountAfter = CountCellsOfType(VT_SUB_NAME);
    DesignerLog::Write(
        "UI_EVENT create_sub new_sub_cmd subs " +
        std::to_string(subCountBefore) + "->" + std::to_string(subCountAfter));
    if (subCountAfter <= subCountBefore) {
        DumpPageHead(8);
        return false;
    }
    DumpPageHead(10);

    // The new sub appends at the bottom: rename the last VT_SUB_NAME cell.
    PageScanResult lastSub;
    {
        int consecutiveMissingRows = 0;
        for (int currentRow = 0; currentRow < kMaximumRowsToScan; ++currentRow) {
            bool rowExists = false;
            for (int currentColumn = 0; currentColumn < kMaximumColumnsToScan; ++currentColumn) {
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
    }
    if (lastSub.matchRow < 0) {
        DesignerLog::Write("UI_EVENT create_sub new_sub_cell=none");
        return false;
    }
    DesignerLog::Write(
        "UI_EVENT create_sub new_sub_cell=\"" +
        WideToUtf8(AnsiToWide(lastSub.matchText)) + "\" row=" +
        std::to_string(lastSub.matchRow) + " col=" +
        std::to_string(lastSub.matchColumn));
    return RenameCellAt(lastSub.matchRow, lastSub.matchColumn, subNameAnsi);
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
    const bool titleMatched = TitleContainsName(title, AnsiToWide(newNameAnsi));
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

struct StatementArea {
    std::vector<std::pair<int, std::string>> lines; // (row, compact text)
    int lastRow = -1;
};

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
    const char* text = statementText.c_str();
    // e5.95's own '.' keyboard handler passes TRUE here. This selects the
    // editor parser path instead of inserting an opaque raw statement cell.
    InvokeIde(FN_INSERT_TEXT, PointerToDword(const_cast<char*>(text)), TRUE);
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
    const std::wstring& subName);
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

// ---------- clipboard page helpers (only used for .参数 row insertion) ----------
// Whole-page clipboard paste is ignored by e5.95 for the .程序集 name, which
// is why assembly renames never use it - but .参数/.子程序 lines DO come
// through, making it the reliable fallback for adding parameter rows.

bool SetClipboardPageCodeWide(std::wstring_view wideText)
{
    if (wideText.empty()) {
        DesignerLog::Write("UI_EVENT clipboard wide_empty");
        return false;
    }
    // Strict ANSI first (WC_NO_BEST_FIT); fall back to best-fit conversion and
    // finally to wide-only clipboard content - the IDE paste reads CF_UNICODETEXT.
    std::string ansiText = WideToAnsi(wideText);
    if (ansiText.empty()) {
        const int required = WideCharToMultiByte(
            CP_ACP, 0, wideText.data(), static_cast<int>(wideText.size()),
            nullptr, 0, nullptr, nullptr);
        if (required > 0) {
            ansiText.assign(static_cast<size_t>(required), '\0');
            WideCharToMultiByte(
                CP_ACP, 0, wideText.data(), static_cast<int>(wideText.size()),
                ansiText.data(), required, nullptr, nullptr);
            while (!ansiText.empty() && ansiText.back() == '\0') {
                ansiText.pop_back();
            }
        }
        DesignerLog::Write(
            "UI_EVENT clipboard ansi_relaxed bytes=" + std::to_string(ansiText.size()));
    }

    for (int attempt = 1; attempt <= 5; ++attempt) {
        if (!OpenClipboard(nullptr)) {
            DesignerLog::Write(
                "UI_EVENT clipboard open_failed err=" +
                std::to_string(GetLastError()) + " attempt=" + std::to_string(attempt));
            PumpMessagesFor(150);
            continue;
        }
        if (!EmptyClipboard()) {
            DesignerLog::Write(
                "UI_EVENT clipboard empty_failed err=" + std::to_string(GetLastError()));
            CloseClipboard();
            continue;
        }
        HGLOBAL wideMemory = GlobalAlloc(
            GMEM_MOVEABLE, (wideText.size() + 1) * sizeof(wchar_t));
        if (wideMemory != nullptr) {
            void* destination = GlobalLock(wideMemory);
            std::memcpy(
                destination, wideText.data(), wideText.size() * sizeof(wchar_t));
            static_cast<wchar_t*>(destination)[wideText.size()] = L'\0';
            GlobalUnlock(wideMemory);
            if (SetClipboardData(CF_UNICODETEXT, wideMemory) == nullptr) {
                DesignerLog::Write(
                    "UI_EVENT clipboard set_utf16_failed err=" +
                    std::to_string(GetLastError()));
                GlobalFree(wideMemory);
            }
        }
        if (!ansiText.empty()) {
            HGLOBAL ansiMemory = GlobalAlloc(GMEM_MOVEABLE, ansiText.size() + 1);
            if (ansiMemory != nullptr) {
                void* destination = GlobalLock(ansiMemory);
                std::memcpy(destination, ansiText.c_str(), ansiText.size() + 1);
                GlobalUnlock(ansiMemory);
                SetClipboardData(CF_TEXT, ansiMemory);
            }
        }
        CloseClipboard();
        DesignerLog::Write(
            "UI_EVENT clipboard set_ok bytes=" +
            std::to_string(wideText.size() * sizeof(wchar_t)));
        return true;
    }
    return false;
}

bool SetClipboardPageCode(std::string_view utf8Text)
{
    return SetClipboardPageCodeWide(Utf8ToWide(utf8Text));
}

bool ReadClipboardPageCode(std::string& pageCode)
{
    pageCode.clear();
    if (!OpenClipboard(nullptr)) {
        return false;
    }
    bool ok = false;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE data = GetClipboardData(CF_UNICODETEXT);
        const wchar_t* text = data != nullptr
            ? static_cast<const wchar_t*>(GlobalLock(data))
            : nullptr;
        if (text != nullptr) {
            pageCode = WideToUtf8(text);
            GlobalUnlock(data);
            ok = !pageCode.empty();
        }
    }
    CloseClipboard();
    return ok;
}

// Restores the previous clipboard text after page-replace operations.
class ClipboardTextGuard {
public:
    ClipboardTextGuard()
    {
        ReadClipboardPageCode(m_previous);
    }
    ~ClipboardTextGuard()
    {
        if (!m_previous.empty()) {
            SetClipboardPageCode(m_previous);
        }
    }
    ClipboardTextGuard(const ClipboardTextGuard&) = delete;
    ClipboardTextGuard& operator=(const ClipboardTextGuard&) = delete;

private:
    std::string m_previous;
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

    bool ok = true;
    for (const SubParamSpec& spec : params) {
        const std::string nameAnsi = WideToAnsi(spec.name);
        const std::string typeAnsi = WideToAnsi(spec.type);
        if (SubHasParamNamed(subRow, nameAnsi)) {
            continue;
        }
        InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(subRow), 0);
        int caretRow = -1;
        int caretColumn = -1;
        QueryCaret(caretRow, caretColumn);
        if (caretRow != subRow) {
            ok = false;
            break;
        }
        InvokeIde(FN_INSERT_NEW_ARG, 0, 0);
        PumpMessagesFor(120);

        int argumentRow = -1;
        for (int row = subRow + 1; row < subRow + 32; ++row) {
            CellText first{};
            if (!ReadCell(row, 0, first) || !CellHasData(first)) continue;
            if (first.type == VT_SUB_NAME && first.isTitle) break;
            if (first.type == VT_SUB_ARG_NAME && !first.isTitle && first.text.empty()) {
                argumentRow = row;
            }
        }
        if (argumentRow < 0) {
            DesignerLog::Write("UI_EVENT sub_param local_row_create=0");
            ok = false;
            break;
        }
        const int nameColumn = FindRowCellColumnByType(argumentRow, VT_SUB_ARG_NAME);
        const int typeColumn = FindRowCellColumnByType(argumentRow, VT_SUB_ARG_TYPE);
        const bool nameSet = nameColumn >= 0 && RenameCellAt(argumentRow, nameColumn, nameAnsi);
        const bool typeSet = typeColumn >= 0 && RenameCellAt(argumentRow, typeColumn, typeAnsi);
        DesignerLog::Write(
            "UI_EVENT sub_param local row=" + std::to_string(argumentRow) +
            " name_set=" + std::to_string(nameSet ? 1 : 0) +
            " type_set=" + std::to_string(typeSet ? 1 : 0));
        if (!nameSet || !typeSet) {
            ok = false;
            break;
        }
    }
    if (ok) {
        for (const SubParamSpec& spec : params) {
            if (!SubHasParamNamed(subRow, WideToAnsi(spec.name))) {
                ok = false;
                break;
            }
        }
    }
    DesignerLog::Write(
        "UI_EVENT sub_param ok=" + std::to_string(ok ? 1 : 0) +
        " method=local_rows sub=\"" + WideToUtf8(AnsiToWide(subNameAnsi)) + "\"");
    return ok;

    // Attempt 2: whole-page clipboard replace with .参数 lines inserted after
    // the .子程序 line. Paste ignores the .程序集 name (harmless here - the
    // assembly name is not being changed) but applies .参数 rows.
    ClipboardTextGuard clipboardGuard;
    RunNativeMenuCommand(mainWindow, kMenuSelectAll, "select_all");
    RunNativeMenuCommand(mainWindow, kMenuCopy, "copy");
    std::string pageText;
    if (!ReadClipboardPageCode(pageText)) {
        DesignerLog::Write("UI_EVENT sub_param page_copy=0");
        return false;
    }
    DesignerLog::Write(
        "UI_EVENT sub_param page_copy=1 bytes=" + std::to_string(pageText.size()));

    // Keep the page and inserted declarations in the same character domain.
    // The IDE clipboard accepts Unicode, while its cell API still uses GBK.
    const std::wstring pageWide = Utf8ToWide(pageText);
    const std::wstring subNameWide = AnsiToWide(subNameAnsi);
    const std::wstring subLinePrefixWide = L".子程序 ";
    size_t insertPosWide = std::wstring::npos;
    size_t subBlockStartWide = std::wstring::npos;
    size_t subBlockEndWide = pageWide.size();
    size_t lineStart = 0;
    while (lineStart <= pageWide.size()) {
        const size_t lineEnd = pageWide.find(L"\r\n", lineStart);
        const size_t lineStop =
            lineEnd == std::wstring::npos ? pageWide.size() : lineEnd;
        const std::wstring line = pageWide.substr(lineStart, lineStop - lineStart);
        if (line.rfind(subLinePrefixWide, 0) == 0) {
            const std::wstring nameToken = line.substr(subLinePrefixWide.size());
            const size_t comma = nameToken.find(L',');
            const std::wstring nameOnly = comma == std::wstring::npos
                ? nameToken
                : nameToken.substr(0, comma);
            if (_wcsicmp(nameOnly.c_str(), subNameWide.c_str()) == 0) {
                insertPosWide = lineStop == pageWide.size() ? pageWide.size() : lineEnd + 2;
                subBlockStartWide = lineStart;
            }
        }
        if (subBlockStartWide != std::wstring::npos && lineStart > subBlockStartWide &&
            line.rfind(subLinePrefixWide, 0) == 0) {
            subBlockEndWide = lineStart;
            break;
        }
        if (lineEnd == std::wstring::npos) {
            break;
        }
        lineStart = lineEnd + 2;
    }
    if (insertPosWide == std::wstring::npos) {
        DesignerLog::Write(
            "UI_EVENT sub_param sub_line_missing=\"" + WideToUtf8(subNameWide) + "\"");
        return false;
    }

    const std::wstring subBlock = pageWide.substr(
        subBlockStartWide, subBlockEndWide - subBlockStartWide);
    bool allParamsInPageText = true;
    for (const SubParamSpec& spec : params) {
        const std::wstring declaration = std::wstring(L".参数 ") + spec.name + L",";
        if (subBlock.find(declaration) == std::wstring::npos) {
            allParamsInPageText = false;
            break;
        }
    }
    if (allParamsInPageText) {
        DesignerLog::Write(
            "UI_EVENT sub_param ok=1 method=page_text_existing sub=\"" +
            WideToUtf8(subNameWide) + "\"");
        return true;
    }

    std::wstring paramLinesWide;
    for (const SubParamSpec& spec : params) {
        paramLinesWide += std::wstring(L".参数 ") + spec.name + L", " +
            spec.type + L"\r\n";
    }
    std::wstring modifiedWide = pageWide;
    modifiedWide.insert(insertPosWide, paramLinesWide);

    if (!SetClipboardPageCodeWide(modifiedWide)) {
        DesignerLog::Write("UI_EVENT sub_param clipboard_set=0");
        return false;
    }
    RunNativeMenuCommand(mainWindow, kMenuSelectAll, "select_all");
    RunNativeMenuCommand(mainWindow, kMenuPaste, "paste");
    PumpMessagesFor(kPasteSettleMs);
    DesignerLog::Write(
        "UI_EVENT sub_param page_paste bytes=" +
        std::to_string(modifiedWide.size() * sizeof(wchar_t)));

    ok = true;
    for (const SubParamSpec& spec : params) {
        if (!SubHasParamNamed(subRow, WideToAnsi(spec.name))) {
            ok = false;
        }
    }
    DesignerLog::Write(
        "UI_EVENT sub_param ok=" + std::to_string(ok ? 1 : 0) +
        " method=page_replace sub=\"" + WideToUtf8(AnsiToWide(subNameAnsi)) + "\"");
    return ok;
}

// Earlier revisions typed ".参数" text into the statement area; those lines
// are plain text, not parameter declarations. Clear them so the corrected
// parameter rows stand alone.
void CleanupBogusParamStatements(const std::string& subNameAnsi, int subRow)
{
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
void SetupSubscribeCallbackBody(
    HWND mainWindow,
    HWND mdiClient,
    HWND document,
    const std::wstring& subNameWide,
    int subRow)
{
    const std::string subAnsi = WideToAnsi(subNameWide);
    if (subAnsi.empty() || subRow < 0) {
        return;
    }
    CleanupBogusParamStatements(subAnsi, subRow);
    const std::vector<SubParamSpec> params = {
        {L"WinId", L"整数型"}, {L"msg", L"文本型"}};
    const bool paramsOk = EnsureSubParams(
        mainWindow, mdiClient, document, subAnsi, subRow, params);
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
    const StatementArea bodyArea = ScanSubStatementArea(subRow);
    bool hasMsgConversion = false;
    bool hasReturnStatement = false;
    int keptMsgRow = -1;
    int keptReturnRow = -1;
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
    hasMsgConversion = keptMsgRow >= 0;
    hasReturnStatement = keptReturnRow >= 0;
    if (!hasMsgConversion && !hasReturnStatement) {
        AppendStatementsIfMissing(
            mdiClient, document, subAnsi, msgText, msgToken, subRow);
        AppendStatementsIfMissing(
            mdiClient, document, subAnsi, returnText, returnToken, subRow);
    }
    else if (!hasMsgConversion) {
        AppendStatementsIfMissing(
            mdiClient, document, subAnsi, msgText, msgToken, subRow);
    }
    else if (!hasReturnStatement) {
        AppendStatementsIfMissing(
            mdiClient, document, subAnsi, returnText, returnToken, subRow);
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

bool JumpToSubroutine(const std::string& subNameAnsi)
{
    const PageScanResult cell = ScanPageForType(VT_SUB_NAME, subNameAnsi, false);
    if (cell.matchRow < 0) {
        return false;
    }
    return InvokeIde(
        FN_MOVE_CARET,
        static_cast<DWORD>(cell.matchRow),
        static_cast<DWORD>(cell.matchColumn));
}

// Locates (or creates) the assembly page and the subroutine inside it. This
// is the shared backbone for normal events and Jade call statements.
PageEnsureResult EnsureAssemblySubPage(
    HWND mainWindow,
    HWND mdiClient,
    const std::wstring& assembly,
    const std::wstring& subName)
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

    HWND targetDocument = FindMdiDocument(mdiClient, assembly);
    if (targetDocument == nullptr) {
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
    else {
        RememberAssemblyDocument(assembly, targetDocument);
    }

    PageScanResult subCell = ScanPageForType(VT_SUB_NAME, subAnsi, false);
    PageScanResult anySub = ScanPageForType(VT_SUB_NAME, std::string(), true);
    for (int attempt = 0; subCell.matchRow < 0 && attempt < 4; ++attempt) {
        PumpMessagesFor(100);
        subCell = ScanPageForType(VT_SUB_NAME, subAnsi, false);
        anySub = ScanPageForType(VT_SUB_NAME, std::string(), true);
    }
    const bool lookingForFixedSubscription =
        _wcsicmp(assembly.c_str(), kSubscribeAssembly) == 0 &&
        _wcsicmp(subName.c_str(), kSubscribeSub) == 0;
    if (subCell.matchRow < 0 && lookingForFixedSubscription) {
        int savedRow = -1;
        int savedColumn = -1;
        QueryCaret(savedRow, savedColumn);
        InvokeIde(FN_MOVE_PAGE_HOME, 0, 0);
        PumpMessagesFor(100);
        subCell = ScanPageForType(VT_SUB_NAME, subAnsi, false);
        anySub = ScanPageForType(VT_SUB_NAME, std::string(), true);
        if (subCell.matchRow < 0) {
            InvokeIde(FN_MOVE_TOP, 0, 0);
            PumpMessagesFor(100);
            subCell = ScanPageForType(VT_SUB_NAME, subAnsi, false);
            anySub = ScanPageForType(VT_SUB_NAME, std::string(), true);
        }
        if (subCell.matchRow < 0) {
            InvokeIde(FN_MOVE_CARET, 0, 0);
            PumpMessagesFor(100);
            subCell = ScanPageForType(VT_SUB_NAME, subAnsi, false);
            anySub = ScanPageForType(VT_SUB_NAME, std::string(), true);
        }
        if (savedRow >= 0 && savedColumn >= 0) {
            InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(savedRow), static_cast<DWORD>(savedColumn));
        }
    }
    const bool textHealthy = anySub.nonEmptyTextCells > 0 || subCell.matchRow >= 0;
    const bool emptyAssemblyPage =
        ScanPageForType(VT_MOD_NAME, std::string(), true).matchRow >= 0;
    const bool fixedSubscriptionMissing =
        lookingForFixedSubscription &&
        textHealthy && !out.createdAssembly;
    DesignerLog::Write(
        "UI_EVENT page_state target=\"" + asmUtf8 + "\" created_new=" +
        std::to_string(out.createdAssembly ? 1 : 0) +
        " sub_exists=" + std::to_string(subCell.matchRow >= 0 ? 1 : 0) +
        " text_healthy=" + std::to_string(textHealthy ? 1 : 0));

    if (subCell.matchRow < 0 && fixedSubscriptionMissing) {
        out.message = "固定子程序索引未稳定，为避免生成第二个 Jade_通讯_订阅 已停止";
        return out;
    }
    if (subCell.matchRow < 0 && (out.createdAssembly || textHealthy || emptyAssemblyPage)) {
        if (!CreateSubroutineAtPage(subAnsi)) {
            PumpMessagesFor(150);
            const PageScanResult recoveredSub =
                ScanPageForType(VT_SUB_NAME, subAnsi, false);
            if (recoveredSub.matchRow >= 0) {
                out.subRow = recoveredSub.matchRow;
                out.document = targetDocument;
                out.ok = true;
                return out;
            }
            out.message = "无法在代码页创建子程序（两种原生路径均未生效）";
            return out;
        }
        out.createdSubroutine = true;
    }
    else if (subCell.matchRow < 0) {
        out.message = "文本回读异常且无法确认子程序是否已存在，为避免重复创建已停止";
        return out;
    }

    if (out.createdAssembly) {
        if (!RenameAssemblyViaApi(targetDocument, asmAnsi)) {
            out.message =
                "通过内部接口重命名为 " + asmUtf8 + " 未通过回读/标题验证，已按规则停止";
            return out;
        }
        RememberAssemblyDocument(assembly, targetDocument);
        DesignerLog::Write(
            "UI_EVENT assembly_renamed name=\"" + asmUtf8 +
            "\" title=\"" + WideToUtf8(GetWindowTitle(targetDocument)) + "\"");
    }
    // Capture the subroutine's row right now, while the page context is
    // freshly verified; a later name re-scan races with the IDE's lazy
    // subroutine index and can fail even though the sub exists.
    const PageScanResult finalSub = ScanPageForType(VT_SUB_NAME, subAnsi, false);
    out.subRow = finalSub.matchRow;
    out.document = targetDocument;
    out.ok = true;
    return out;
}

} // namespace

namespace IdeEventRouter {

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
    // Channel-subscription events may arrive without a handler name; the
    // router derives ipc_xxx from the channel mapping table.
    return !event.handlerName.empty() || !event.callType.empty();
}

RouteResult Route(HWND mainWindow, HWND mdiClient, const UiEvent& event)
{
    if (mainWindow == nullptr || mdiClient == nullptr ||
        !IsWindow(mainWindow) || !IsWindow(mdiClient)) {
        return Fail("invalid_context", "易语言编辑窗口尚未准备好");
    }
    if (g_routingInProgress.exchange(true)) {
        return Fail("busy", "上一个事件仍在处理中，请稍候再操作");
    }
    RoutingGuard routingGuard;

    std::wstring handler = Utf8ToWide(event.handlerName);
    if (event.callType == "JadeView.通讯.订阅" && handler.empty() && !event.callParam.empty()) {
        handler = DeriveIpcNameFromChannel(Utf8ToWide(event.callParam));
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
        const bool jumped = JumpToSubroutine(registerSubAnsi);
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

    if (_wcsicmp(assembly.c_str(), kSubscribeAssembly) == 0 &&
        FindMdiDocument(mdiClient, std::wstring(kSubscribeAssembly)) == nullptr) {
        std::string hookError;
        const bool generated = HookBridge::GenerateAssembly(
            assemblyAnsi, channelAnsi, handlerAnsi, hookError);
        DesignerLog::Write(
            "HYBRID first_generate success=" + std::to_string(generated ? 1 : 0) +
            " error=\"" + hookError + "\"");
        if (generated) {
            PumpMessagesFor(200);
            return {
                true,
                "hook_create",
                "Hook 已生成程序集 " + assemblyUtf8 + " → " + handlerUtf8};
        }
    }

    // Create the fixed subscription routine first. This keeps it at the top
    // of a fresh assembly, before callback subroutines are added.
    const PageEnsureResult hookPage = EnsureAssemblySubPage(
        mainWindow, mdiClient, std::wstring(kSubscribeAssembly), std::wstring(kSubscribeSub));
    if (!hookPage.ok) {
        return Fail("subscribe", "固定子程序准备失败：" + hookPage.message);
    }
    PageEnsureResult result;
    if (_wcsicmp(assembly.c_str(), kSubscribeAssembly) == 0) {
        const PageScanResult callbackExists = ScanPageForType(VT_SUB_NAME, handlerAnsi, false);
        if (callbackExists.matchRow >= 0) {
            result.ok = true;
            result.document = hookPage.document;
            result.subRow = callbackExists.matchRow;
        }
        else {
            ActivateDocument(mdiClient, hookPage.document);
            InvokeIde(FN_MOVE_BOTTOM, 0, 0);
            const std::string callbackText =
                WideToAnsi(L".子程序 ") + handlerAnsi + WideToAnsi(L", 整数型\r\n") +
                WideToAnsi(L".参数 WinId, 整数型\r\n") +
                WideToAnsi(L".参数 msg, 文本型\r\n\r\n") +
                WideToAnsi(L"msg ＝ UTF8文本到GBK文本 (msg)\r\n") +
                WideToAnsi(L"返回 (JadeView.文本.创建指针 (\"ok\"))\r\n");
            std::string hookError;
            const bool inserted = HookBridge::InsertAnsi(callbackText, hookError);
            PumpMessagesFor(120);
            const PageScanResult callback = ScanPageForType(VT_SUB_NAME, handlerAnsi, false);
            // The native paste routine has already parsed and committed the
            // callback. A long page may hide the newly inserted row from the
            // public grid reader, so do not turn a successful native commit
            // into a false failure just because immediate rediscovery missed it.
            result.ok = inserted;
            result.document = hookPage.document;
            result.subRow = callback.matchRow;
            DesignerLog::Write(
                "HYBRID callback_hook success=" + std::to_string(result.ok ? 1 : 0) +
                " error=\"" + hookError + "\"");
        }
    }
    else {
        result = EnsureAssemblySubPage(mainWindow, mdiClient, assembly, handler);
    }
    if (!result.ok) {
        return Fail(result.createdAssembly ? "create_assembly" : "create_sub", result.message);
    }
    // Give the callback its production signature (WinId/msg 参数、UTF-8 提醒、
    // 返回响应指针与整数型返回值) — mirrors ipc_获取二维码 in the reference
    // project; no-op when the signature already exists.
    SetupSubscribeCallbackBody(mainWindow, mdiClient, result.document, handler, result.subRow);

    // Wire the control into Jade_通讯_订阅集.Jade_通讯_订阅.
    const std::string subscribeSubAnsi = WideToAnsi(kSubscribeSub);
    const std::string statementText =
        WideToAnsi(L"JadeView.通讯.订阅 (\"") + channelAnsi +
        WideToAnsi(L"\", &") + handlerAnsi + ")\r\n";
    const std::string detectToken =
        WideToAnsi(L"JadeView.通讯.订阅(\"") + channelAnsi + "\"";
    const std::string compactDetectToken = CompactStatement(detectToken);
    // Parameter page replacement can shift every following physical row.
    // Re-locate the fixed subscription routine immediately before writing it.
    PageEnsureResult currentHookPage = hookPage;
    const PageScanResult refreshedHook =
        ScanPageForType(VT_SUB_NAME, subscribeSubAnsi, false);
    if (refreshedHook.matchRow >= 0) {
        currentHookPage.subRow = refreshedHook.matchRow;
    }
    bool hookAppended = false;
    const StatementArea hookArea = ScanSubStatementArea(currentHookPage.subRow);
    bool hookAlreadyPresent = false;
    for (const auto& entry : hookArea.lines) {
        if (entry.second.find(compactDetectToken) != std::string::npos) {
            hookAlreadyPresent = true;
            break;
        }
    }
    if (!hookAlreadyPresent) {
        ActivateDocument(mdiClient, currentHookPage.document);
        int appendRow = currentHookPage.subRow + 1;
        if (hookArea.lastRow >= appendRow) {
            appendRow = hookArea.lastRow;
            CellText lastCell{};
            if (ReadCell(appendRow, 0, lastCell) && !lastCell.text.empty()) {
                InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(appendRow), 0);
                InvokeIde(FN_INSERT_NEW_AT_NEXT, 0, 0);
                PumpMessagesFor(80);
                ++appendRow;
            }
        }
        InvokeIde(FN_MOVE_CARET, static_cast<DWORD>(appendRow), 0);
        InvokeIde(FN_MOVE_EDIT_CARET_TO_END, 0, 0);
        std::string hookError;
        hookAppended = HookBridge::InsertAnsi(statementText, hookError);
        DesignerLog::Write(
            "HYBRID subscription_hook success=" + std::to_string(hookAppended ? 1 : 0) +
            " error=\"" + hookError + "\"");
    }
    else {
        hookAppended = true;
    }

    const bool jumped = JumpToSubroutine(handlerAnsi);
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
