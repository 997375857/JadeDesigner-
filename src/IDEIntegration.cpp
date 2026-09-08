#include "IDEIntegration.h"

#include "DesignerLog.h"
#include "HookBridge.h"
#include "WebPreview.h"

#include <CommCtrl.h>

#include <cwchar>
#include <iterator>
#include <string>

namespace {

constexpr int kCodeTabControlId = 59392;
constexpr int kMdiClientControlId = 59648;
constexpr UINT_PTR kMainSubclassId = 0x4A440001;
constexpr UINT_PTR kAttachTimerId = 0x4A45;
constexpr UINT kInitialAttachDelayMs = 1200;

struct IntegrationState {
    HMODULE module = nullptr;
    HWND mainWindow = nullptr;
    HWND mdiClient = nullptr;
    HWND codeTab = nullptr;
    bool mainSubclassed = false;
    bool attached = false;
};

IntegrationState g_state;

std::wstring GetClassNameSafe(HWND window)
{
    wchar_t buffer[256]{};
    const int length = GetClassNameW(window, buffer, static_cast<int>(std::size(buffer)));
    return length > 0 ? std::wstring(buffer, length) : std::wstring();
}

HWND FindDirectChild(HWND parent, int controlId, const wchar_t* expectedClass)
{
    HWND child = GetWindow(parent, GW_CHILD);
    while (child != nullptr) {
        if (GetParent(child) == parent && GetDlgCtrlID(child) == controlId) {
            const std::wstring className = GetClassNameSafe(child);
            if (expectedClass == nullptr || _wcsicmp(className.c_str(), expectedClass) == 0) {
                return child;
            }
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
    return nullptr;
}

bool TryAttach()
{
    if (g_state.attached) {
        if (WebPreview::IsAttached()) {
            WebPreview::Layout();
            return true;
        }
        g_state.attached = false;
    }
    if (g_state.mainWindow == nullptr || !IsWindow(g_state.mainWindow)) {
        return false;
    }

    const HWND codeTab = FindDirectChild(
        g_state.mainWindow, kCodeTabControlId, L"CCustomTabCtrl");
    const HWND mdiClient = FindDirectChild(
        g_state.mainWindow, kMdiClientControlId, L"MDIClient");
    if (codeTab == nullptr || mdiClient == nullptr) {
        return false;
    }

    g_state.codeTab = codeTab;
    g_state.mdiClient = mdiClient;
    g_state.attached = WebPreview::Attach(g_state.mainWindow, mdiClient, codeTab);
    if (g_state.attached) {
        DesignerLog::Write(
            "IDE target attached code_tab=" + DesignerLog::HexPointer(codeTab) +
            " class=CCustomTabCtrl id=59392 mdi=" + DesignerLog::HexPointer(mdiClient));
    }
    return g_state.attached;
}

LRESULT CALLBACK MainSubclassProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR,
    DWORD_PTR)
{
    switch (message) {
    case WM_TIMER:
        if (wParam == kAttachTimerId) {
            TryAttach();
            return 0;
        }
        break;
    case WM_SIZE:
    case WM_WINDOWPOSCHANGED: {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        WebPreview::Layout();
        return result;
    }
    case WM_NCDESTROY:
        KillTimer(window, kAttachTimerId);
        g_state.mainSubclassed = false;
        WebPreview::Shutdown();
        g_state.mainWindow = nullptr;
        g_state.mdiClient = nullptr;
        g_state.codeTab = nullptr;
        g_state.attached = false;
        break;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

namespace IDEIntegration {

void InitializeModule(HMODULE module)
{
    g_state.module = module;
    WebPreview::InitializeModule(module);
}

bool Start(HWND mainWindow)
{
    if (mainWindow == nullptr || !IsWindow(mainWindow)) {
        DesignerLog::Write("IDE start failed: invalid main window");
        return false;
    }
    if (g_state.mainWindow != nullptr && g_state.mainWindow != mainWindow) {
        Stop();
    }

    g_state.mainWindow = mainWindow;
    if (!g_state.mainSubclassed) {
        g_state.mainSubclassed = SetWindowSubclass(
            mainWindow, MainSubclassProc, kMainSubclassId, 0) != FALSE;
        if (!g_state.mainSubclassed) {
            DesignerLog::Write(
                "IDE SetWindowSubclass(main) failed error=" + std::to_string(GetLastError()));
            return false;
        }
    }
    // e5.95 can still be constructing/switching the source MDI page when it
    // sends NL_IDE_READY. Defer MDI/WebView creation until that transition has
    // settled; explicit preview commands still attach immediately.
    SetTimer(mainWindow, kAttachTimerId, kInitialAttachDelayMs, nullptr);
    // Which executable the plugin is loaded into decides whether the memory
    // bridge can work at all, and nothing later in the log reveals it: every
    // hook failure looks the same from the outside.
    const HookBridge::HostInfo host = HookBridge::InspectHost();
    DesignerLog::Write(
        "IDE start main=" + DesignerLog::HexPointer(mainWindow) +
        " initial_attach=deferred delay_ms=" + std::to_string(kInitialAttachDelayMs) +
        " host=\"" + host.exeNameUtf8 +
        "\" host_name_ok=" + std::to_string(host.nameSupported ? 1 : 0) +
        " host_build_ok=" + std::to_string(host.buildSupported ? 1 : 0));
    return true;
}

void TogglePreview()
{
    if (TryAttach()) {
        WebPreview::Toggle();
    }
    else {
        DesignerLog::Write("IDE toggle skipped: target controls are not ready");
    }
}

void RefreshPreview()
{
    if (TryAttach()) {
        WebPreview::Refresh();
    }
    else {
        DesignerLog::Write("IDE refresh skipped: target controls are not ready");
    }
}

void Stop()
{
    if (g_state.mainWindow != nullptr && IsWindow(g_state.mainWindow)) {
        KillTimer(g_state.mainWindow, kAttachTimerId);
    }
    if (g_state.mainSubclassed && g_state.mainWindow != nullptr && IsWindow(g_state.mainWindow)) {
        RemoveWindowSubclass(g_state.mainWindow, MainSubclassProc, kMainSubclassId);
    }
    g_state.mainSubclassed = false;
    WebPreview::Shutdown();
    g_state.mainWindow = nullptr;
    g_state.mdiClient = nullptr;
    g_state.codeTab = nullptr;
    g_state.attached = false;
    DesignerLog::Write("IDE integration stopped");
}

} // namespace IDEIntegration
