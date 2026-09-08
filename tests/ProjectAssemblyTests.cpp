#include "../src/ProjectAssembly.h"
#include "../src/PageSource.h"
#include <CommCtrl.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
HWND mainWindow, tree, mdi;
HWND activeDocument = nullptr;
int jumps = 0;
int passed = 0;
bool failReads = false;
bool allowOpen = true;

void Check(bool value, const char* name)
{
    if (!value) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
    ++passed;
    std::printf("PASS %s\n", name);
}

HWND AddDocument(const wchar_t* title)
{
    const HWND document = CreateWindowExW(0, L"STATIC", title, WS_CHILD,
        0, 0, 100, 100, mdi, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(document, GWL_EXSTYLE, WS_EX_MDICHILD);
    return document;
}

LRESULT CALLBACK MainProc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_COMMAND && LOWORD(wp) == 32793) {
        ++jumps;
        if (allowOpen) {
            activeDocument = ProjectAssembly::FindOpenDocument(mdi, L"Jade_通讯_订阅集");
            if (!activeDocument) activeDocument = AddDocument(L"程序集: Jade_通讯_订阅集");
        }
        return 0;
    }
    return DefWindowProcW(window, message, wp, lp);
}

LRESULT CALLBACK TreeProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (failReads && message == TVM_GETITEMW) return FALSE;
    return DefSubclassProc(window, message, wp, lp);
}

LRESULT CALLBACK MdiProc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (message == WM_MDIGETACTIVE) return reinterpret_cast<LRESULT>(activeDocument);
    return DefSubclassProc(window, message, wp, lp);
}

HTREEITEM AddItem(const wchar_t* text, int image, HTREEITEM parent = TVI_ROOT, int children = 0)
{
    TVINSERTSTRUCTW data{};
    data.hParent = parent;
    data.hInsertAfter = TVI_LAST;
    data.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_CHILDREN;
    data.item.pszText = const_cast<wchar_t*>(text);
    data.item.iImage = image;
    data.item.cChildren = children;
    return reinterpret_cast<HTREEITEM>(SendMessageW(tree, TVM_INSERTITEMW, 0,
        reinterpret_cast<LPARAM>(&data)));
}
}

int main()
{
    using namespace ProjectAssembly;
    const wchar_t* wanted = L"Jade_通讯_订阅集";
    INITCOMMONCONTROLSEX init{sizeof(init), ICC_TREEVIEW_CLASSES};
    InitCommonControlsEx(&init);
    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = L"Jade.ProjectAssembly.Test";
    wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    mainWindow = CreateWindowW(wc.lpszClassName, L"test", 0, 0, 0, 200, 200,
        nullptr, nullptr, wc.hInstance, nullptr);
    mdi = CreateWindowW(L"STATIC", L"", WS_CHILD, 0, 0, 100, 100,
        mainWindow, nullptr, wc.hInstance, nullptr);
    SetWindowSubclass(mdi, MdiProc, 1, 0);

    Check(Find(mainWindow, wanted).state == State::Unavailable, "missing tree is unknown");
    tree = CreateWindowW(WC_TREEVIEWW, L"", WS_CHILD, 0, 0, 100, 100,
        mainWindow, reinterpret_cast<HMENU>(1019), wc.hInstance, nullptr);
    SetWindowSubclass(tree, TreeProc, 1, 0);
    Check(Find(mainWindow, wanted).state == State::Unavailable, "empty tree is unknown");
    HTREEITEM root = AddItem(L"程序数据", 8);
    auto lookup = Find(mainWindow, wanted);
    Check(lookup.state == State::Absent && lookup.nodes == 1, "complete empty project proves absence");
    HTREEITEM assembly = AddItem(wanted, 3, root);
    AddItem(L"Jade_通讯_订阅", 2, assembly);
    Check(Find(mainWindow, wanted).state == State::Found, "collapsed closed assembly exists");
    auto other = AddDocument(L"程序集: Jade_通讯_订阅集_backup");
    Check(FindOpenDocument(mdi, wanted) == nullptr, "prefix title is not a match");
    lookup = FindAndOpen(mainWindow, mdi, wanted);
    Check(lookup.state == State::Found && lookup.document && jumps == 1,
        "closed existing assembly opens without creation");
    const HWND original = lookup.document;
    lookup = FindAndOpen(mainWindow, mdi, wanted);
    Check(lookup.document == original && jumps == 1, "open assembly reused");
    Check(JumpToSubroutine(mainWindow, mdi, wanted, L"Jade_通讯_订阅"), "unique subroutine uses native jump");
    const int beforeMissingSub = jumps;
    Check(!JumpToSubroutine(mainWindow, mdi, wanted, L"missing") && jumps == beforeMissingSub,
        "missing subroutine does not jump");
    auto duplicateSub = AddItem(L"Jade_通讯_订阅", 2, assembly);
    Check(!JumpToSubroutine(mainWindow, mdi, wanted, L"Jade_通讯_订阅") && jumps == beforeMissingSub,
        "duplicate subroutine blocks native jump");
    TreeView_DeleteItem(tree, duplicateSub);
    SetWindowTextW(original, L"程序集: renamed");
    Check(FindOpenDocument(mdi, wanted) == nullptr, "renamed window is not cached");
    DestroyWindow(original);
    allowOpen = false;
    lookup = FindAndOpen(mainWindow, mdi, wanted);
    Check(lookup.state == State::Found && !lookup.document, "open failure cannot become absent");
    allowOpen = true;
    auto duplicate = AddItem(wanted, 3, root);
    const int before = jumps;
    Check(FindAndOpen(mainWindow, mdi, wanted).state == State::Ambiguous && jumps == before,
        "duplicate assembly blocks opening and creation");
    TreeView_DeleteItem(tree, duplicate);
    failReads = true;
    Check(Find(mainWindow, wanted).state == State::Unavailable, "read failure cannot prove absence");
    failReads = false;
    auto lazy = AddItem(L"lazy", 15, root, 1);
    Check(Find(mainWindow, wanted).state == State::Unavailable, "unloaded children block decisions");
    TreeView_DeleteItem(tree, lazy);
    TreeView_DeleteItem(tree, assembly);
    Check(Find(mainWindow, wanted).state == State::Absent, "deleted assembly is no longer remembered");
    const HWND pendingPage = AddDocument(L"程序集: Jade_通讯_订阅集");
    Check(FindAndOpen(mainWindow, mdi, wanted).state == State::Unavailable,
        "open page conflicts with a stale empty tree");
    DestroyWindow(pendingPage);
    auto folder = AddItem(L"folder", 15, root);
    AddItem(wanted, 3, folder);
    Check(Find(mainWindow, wanted).state == State::Found, "nested assembly is found");
    TreeView_DeleteAllItems(tree);
    AddItem(L"other project not ready", 8);
    Check(Find(mainWindow, wanted).state == State::Unavailable, "unknown root blocks creation");
    Check(MatchesDocumentTitle(L"程序集: JADE_通讯_订阅集", wanted), "case insensitive exact name");
    Check(!MatchesDocumentTitle(L"窗口: Jade_通讯_订阅集", wanted), "non-code document rejected");
    Check(!MatchesDocumentTitle(L"程序集: Jade_通讯_订阅集2", wanted), "name suffix rejected");
    const auto page = PageSource::ParsePageCode(
        ".版本 2\r\n\r\n.程序集 UI_JadeView\r\n.程序集变量 窗口, 整数型\r\n\r\n.子程序 测试, 整数型\r\n");
    Check(page.valid && page.assemblyUtf8 == "UI_JadeView", "assembly variables do not replace assembly name");
    Check(page.subNamesUtf8.size() == 1 && page.subNamesUtf8[0] == "测试" && page.subLinesFromText[0] == 5,
        "subroutine text line preserves blank lines");
    Check(!PageSource::ParsePageCode(".程序集变量 窗口, 整数型\n.子程序 测试").valid,
        "variable directive cannot prove readable assembly");
    Check(!PageSource::ParsePageCode(".程序集 A\n.程序集 B\n").valid,
        "multiple assembly headers rejected");
    Check(PageSource::ParsePageCode("\t.程序集\tA\n .子程序\tB").valid,
        "tabs and indentation accepted");
    DestroyWindow(other);
    DestroyWindow(mainWindow);
    std::printf("%d checks passed\n", passed);
}
