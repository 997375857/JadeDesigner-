#include "ProjectAssembly.h"

#include <CommCtrl.h>
#include <cwchar>
#include <set>
#include <vector>

namespace {

// e5.95 program workspace, verified with the project copy on 2026-09-08.
constexpr int kProgramTreeId = 1019;
constexpr int kAssemblyImage = 3;
constexpr UINT kJumpToProgramItem = 32793; // menu resource 238: G. jump
constexpr int kMaxItems = 100000;

bool Equal(std::wstring_view left, std::wstring_view right)
{
    return left.size() == right.size() &&
        _wcsnicmp(left.data(), right.data(), left.size()) == 0;
}

bool ReadItem(HWND tree, HTREEITEM handle, TVITEMW& item, std::wstring& text)
{
    wchar_t buffer[1024]{};
    item = {};
    item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_CHILDREN;
    item.hItem = handle;
    item.pszText = buffer;
    item.cchTextMax = static_cast<int>(sizeof(buffer) / sizeof(buffer[0]));
    if (!SendMessageW(tree, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item))) {
        return false;
    }
    text = buffer;
    return text.size() + 1 < sizeof(buffer) / sizeof(buffer[0]);
}

HTREEITEM Next(HWND tree, UINT relation, HTREEITEM item = nullptr)
{
    return reinterpret_cast<HTREEITEM>(SendMessageW(
        tree, TVM_GETNEXTITEM, relation, reinterpret_cast<LPARAM>(item)));
}

BOOL CALLBACK FindTrees(HWND window, LPARAM parameter)
{
    if (GetDlgCtrlID(window) != kProgramTreeId) {
        return TRUE;
    }
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    if (_wcsicmp(name, WC_TREEVIEWW) == 0) {
        reinterpret_cast<std::vector<HWND>*>(parameter)->push_back(window);
    }
    return TRUE;
}

} // namespace

namespace ProjectAssembly {

bool MatchesDocumentTitle(std::wstring_view title, std::wstring_view name)
{
    if (name.empty()) {
        return false;
    }
    // Do not accept a prefix match (Foo vs Foo_backup), or text in a form title.
    constexpr std::wstring_view prefix = L"程序集: ";
    return title.size() > prefix.size() &&
        title.substr(0, prefix.size()) == prefix &&
        Equal(title.substr(prefix.size()), name);
}

HWND FindOpenDocument(HWND mdiClient, std::wstring_view name)
{
    HWND found = nullptr;
    for (HWND child = GetWindow(mdiClient, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if (GetParent(child) != mdiClient ||
            (GetWindowLongPtrW(child, GWL_EXSTYLE) & WS_EX_MDICHILD) == 0) {
            continue;
        }
        wchar_t title[2048]{};
        const int copied = GetWindowTextW(child, title, 2048);
        if (copied <= 0 || copied >= 2047 || !MatchesDocumentTitle(title, name)) {
            continue;
        }
        if (found != nullptr) {
            return nullptr;
        }
        found = child;
    }
    return found;
}

Lookup Find(HWND mainWindow, std::wstring_view name)
{
    Lookup out;
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(mainWindow, &process);
    if (name.empty() || !IsWindow(mainWindow) || process != GetCurrentProcessId() ||
        thread != GetCurrentThreadId()) {
        out.reason = "invalid_context_or_thread";
        return out;
    }
    std::vector<HWND> trees;
    EnumChildWindows(mainWindow, FindTrees, reinterpret_cast<LPARAM>(&trees));
    if (trees.size() != 1) {
        out.reason = "program_tree_missing_or_ambiguous";
        return out;
    }
    out.tree = trees.front();
    const LRESULT count = SendMessageW(out.tree, TVM_GETCOUNT, 0, 0);
    HTREEITEM root = Next(out.tree, TVGN_ROOT);
    TVITEMW item{};
    std::wstring label;
    if (count <= 0 || count > kMaxItems || !root ||
        !ReadItem(out.tree, root, item, label) || label != L"程序数据" ||
        Next(out.tree, TVGN_NEXT, root) != nullptr) {
        out.reason = "program_tree_not_ready";
        return out;
    }
    std::vector<HTREEITEM> pending{root};
    std::set<HTREEITEM> visited;
    bool conflictingKind = false;
    while (!pending.empty()) {
        const HTREEITEM current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second || ++out.nodes > count ||
            !ReadItem(out.tree, current, item, label)) {
            out.reason = "program_tree_read_failed";
            return out;
        }
        if (Equal(label, name) && item.iImage == kAssemblyImage) {
            ++out.matches;
            out.item = reinterpret_cast<LPARAM>(current);
        }
        else if (Equal(label, name) && item.iImage != 2) {
            // A different program-item kind under that name cannot authorize
            // inserting an assembly. Image 2 is a subroutine in this tree.
            conflictingKind = true;
        }
        const HTREEITEM child = Next(out.tree, TVGN_CHILD, current);
        if (!child && item.cChildren != 0) {
            out.reason = "program_tree_children_unavailable";
            return out;
        }
        if (child) pending.push_back(child);
        if (const HTREEITEM sibling = Next(out.tree, TVGN_NEXT, current)) {
            pending.push_back(sibling);
        }
    }
    if (out.nodes != count || SendMessageW(out.tree, TVM_GETCOUNT, 0, 0) != count) {
        out.reason = "program_tree_changed";
        return out;
    }
    out.state = conflictingKind || out.matches > 1 ? State::Ambiguous
        : out.matches == 1 ? State::Found : State::Absent;
    out.reason = out.state == State::Ambiguous ? "duplicate_name_or_kind_conflict"
        : out.state == State::Found ? "existing_project_item" : "complete_tree_absence";
    return out;
}

Lookup FindAndOpen(HWND mainWindow, HWND mdiClient, std::wstring_view name)
{
    Lookup out = Find(mainWindow, name);
    if (out.state == State::Absent) {
        // The tree can lag an insertion until the IDE returns to its message
        // loop. An already-open page is conflicting evidence, never absence.
        for (HWND child = GetWindow(mdiClient, GW_CHILD); child;
             child = GetWindow(child, GW_HWNDNEXT)) {
            wchar_t title[2048]{};
            GetWindowTextW(child, title, 2048);
            if (MatchesDocumentTitle(title, name)) {
                out.state = State::Unavailable;
                out.reason = "project_tree_page_disagree";
                return out;
            }
        }
    }
    if (out.state != State::Found) return out;
    out.document = FindOpenDocument(mdiClient, name);
    if (out.document) return out;

    // The workspace's native Jump command opens code pages even when the
    // page was never opened this session. No create command is sent here.
    SendMessageW(out.tree, TVM_SELECTITEM, TVGN_CARET, out.item);
    if (reinterpret_cast<LPARAM>(Next(out.tree, TVGN_CARET)) != out.item) {
        out.reason = "program_item_selection_failed";
        return out;
    }
    SendMessageW(mainWindow, WM_COMMAND, kJumpToProgramItem, 0);
    out.document = FindOpenDocument(mdiClient, name);
    out.reason = out.document ? "opened_project_item" : "existing_page_open_failed";
    return out;
}

std::vector<std::wstring> ListNames(HWND mainWindow)
{
    std::vector<std::wstring> out;
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(mainWindow, &process);
    if (!IsWindow(mainWindow) || process != GetCurrentProcessId() ||
        thread != GetCurrentThreadId()) return out;
    std::vector<HWND> trees;
    EnumChildWindows(mainWindow, FindTrees, reinterpret_cast<LPARAM>(&trees));
    if (trees.size() != 1) return out;
    const HWND tree = trees.front();
    const LRESULT count = SendMessageW(tree, TVM_GETCOUNT, 0, 0);
    HTREEITEM root = Next(tree, TVGN_ROOT);
    TVITEMW item{};
    std::wstring label;
    if (count <= 0 || count > kMaxItems || !root ||
        !ReadItem(tree, root, item, label) || label != L"程序数据" ||
        Next(tree, TVGN_NEXT, root) != nullptr) return out;
    std::vector<HTREEITEM> pending{root};
    std::set<HTREEITEM> visited;
    while (!pending.empty()) {
        const HTREEITEM current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second || visited.size() > static_cast<size_t>(count) ||
            !ReadItem(tree, current, item, label)) return {};
        if (current != root && item.iImage == kAssemblyImage && !label.empty()) {
            out.push_back(label);
        }
        const HTREEITEM child = Next(tree, TVGN_CHILD, current);
        if (!child && item.cChildren != 0) return {};
        if (child) pending.push_back(child);
        if (const HTREEITEM sibling = Next(tree, TVGN_NEXT, current)) pending.push_back(sibling);
    }
    if (visited.size() != static_cast<size_t>(count) ||
        SendMessageW(tree, TVM_GETCOUNT, 0, 0) != count) return {};
    return out;
}

std::vector<std::wstring> ListUserAssemblies(HWND mainWindow)
{
    std::vector<std::wstring> out;
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(mainWindow, &process);
    if (!IsWindow(mainWindow) || process != GetCurrentProcessId() ||
        thread != GetCurrentThreadId()) return out;
    std::vector<HWND> trees;
    EnumChildWindows(mainWindow, FindTrees, reinterpret_cast<LPARAM>(&trees));
    if (trees.size() != 1) return out;
    const HWND tree = trees.front();
    const LRESULT count = SendMessageW(tree, TVM_GETCOUNT, 0, 0);
    HTREEITEM root = Next(tree, TVGN_ROOT);
    TVITEMW item{};
    std::wstring label;
    if (count <= 0 || count > kMaxItems || !root ||
        !ReadItem(tree, root, item, label) || label != L"程序数据" ||
        Next(tree, TVGN_NEXT, root) != nullptr) return out;
    for (HTREEITEM child = Next(tree, TVGN_CHILD, root); child;
         child = Next(tree, TVGN_NEXT, child)) {
        if (!ReadItem(tree, child, item, label)) return {};
        if (item.iImage == kAssemblyImage && !label.empty()) out.push_back(label);
    }
    return out;
}

bool JumpToSubroutine(HWND mainWindow, HWND mdiClient,
    std::wstring_view assembly, std::wstring_view subroutine)
{
    const Lookup found = Find(mainWindow, assembly);
    if (found.state != State::Found) return false;
    HTREEITEM match = nullptr;
    for (HTREEITEM child = Next(found.tree, TVGN_CHILD, reinterpret_cast<HTREEITEM>(found.item));
         child; child = Next(found.tree, TVGN_NEXT, child)) {
        TVITEMW item{};
        std::wstring text;
        if (!ReadItem(found.tree, child, item, text)) return false;
        if (item.iImage == 2 && Equal(text, subroutine)) {
            if (match) return false;
            match = child;
        }
    }
    if (!match) return false;
    SendMessageW(found.tree, TVM_SELECTITEM, TVGN_CARET, reinterpret_cast<LPARAM>(match));
    if (Next(found.tree, TVGN_CARET) != match) return false;
    SendMessageW(mainWindow, WM_COMMAND, kJumpToProgramItem, 0);
    const HWND document = FindOpenDocument(mdiClient, assembly);
    return document && reinterpret_cast<HWND>(SendMessageW(mdiClient, WM_MDIGETACTIVE, 0, 0)) == document;
}

} // namespace ProjectAssembly
