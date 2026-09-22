#define NOMINMAX
#include <Windows.h>
#include <fnshare.h>
#include <lib2.h>
#include "../src/DesignerLog.h"
#include "../src/IDEIntegration.h"
#include <cstdio>
#include <cstdlib>

extern "C" PLIB_INFOX WINAPI GetNewInf();
namespace {
int initialized = 0, starts = 0, stops = 0, logs = 0, toggles = 0;
HWND mainWindow = nullptr;
void Check(bool ok, const char* name) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
    std::printf("PASS %s\n", name);
}
INT WINAPI SystemNotify(INT message, DWORD, DWORD) {
    return message == NES_GET_MAIN_HWND ? reinterpret_cast<INT>(mainWindow) : 0;
}
// An exception after FreeLibrary exercises the process-wide handler list.
bool ExceptionAfterUnload(DWORD code) {
    __try { RaiseException(code, 0, 0, nullptr); }
    __except (GetExceptionCode() == code ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
}
namespace DesignerLog {
void Initialize(HMODULE) { ++logs; }
void Write(std::string_view) { ++logs; }
std::string HexPointer(const void*) { return "test"; }
}
namespace IDEIntegration {
void InitializeModule(HMODULE) { ++initialized; }
bool Start(HWND window) { ++starts; return window == mainWindow; }
void Stop() { ++stops; }
void TogglePreview() { ++toggles; }
void RefreshPreview() {}
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    auto info = GetNewInf();
    for (int i = 0; i < 20; ++i) Check(GetNewInf() == info, "metadata address is stable");
    Check(info && info->m_pfnNotify && initialized == 0 && logs == 0, "metadata query has no runtime initialization or logging");
    auto notify = info->m_pfnNotify;
    notify(NL_GET_CMD_FUNC_NAMES, 0, 0);
    notify(NL_GET_NOTIFY_LIB_FUNC_NAME, 0, 0);
    notify(NL_GET_DEPENDENT_LIBS, 0, 0);
    notify(NL_FREE_LIB_DATA, 0, 0);
    Check(initialized == 0 && stops == 0 && logs == 0, "scan notifications and cleanup stay inert");
    Check(notify(NL_IDE_READY, 0, 0) == NR_ERR && initialized == 0, "ready without NotifySys cannot attach");
    mainWindow = CreateWindowExW(0, L"STATIC", L"lifecycle test", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    notify(NL_SYS_NOTIFY_FUNCTION, reinterpret_cast<DWORD>(&SystemNotify), 0);
    Check(initialized == 0 && logs == 0, "system callback registration alone does not start plugin");
    Check(notify(NL_IDE_READY, 0, 0) == NR_OK && initialized == 1 && starts == 1, "formal enable initializes and starts plugin");
    GetNewInf(); Check(initialized == 1 && starts == 1, "metadata scan while enabled does not attach again");
    notify(NL_UNLOAD_FROM_IDE, 0, 0); notify(NL_FREE_LIB_DATA, 0, 0);
    Check(stops == 1, "disable and free notifications clean up once");
    info->m_pfnRunAddInFn(0);
    Check(toggles == 0 && starts == 1, "disabled plugin cannot run preview with stale NotifySys");
    notify(NL_SYS_NOTIFY_FUNCTION, reinterpret_cast<DWORD>(&SystemNotify), 0);
    notify(NL_IDE_READY, 0, 0); info->m_pfnRunAddInFn(0);
    Check(initialized == 2 && toggles == 1, "plugin can enable again and run preview command");
    notify(NL_UNLOAD_FROM_IDE, 0, 0); DestroyWindow(mainWindow);
    if (argc != 2) return 0;
    for (int i = 0; i < 50; ++i) {
        HMODULE library = LoadLibraryW(argv[1]);
        Check(library != nullptr, "load compiled support library");
        auto getInfo = reinterpret_cast<PLIB_INFOX (WINAPI*)()>(GetProcAddress(library, "GetNewInf"));
        Check(getInfo != nullptr && getInfo()->m_pfnNotify != nullptr, "compiled metadata is readable");
        // Deliberately omit unload notifications, as the config scanner does.
        Check(FreeLibrary(library) != FALSE && GetModuleHandleW(argv[1]) == nullptr, "temporary support library actually unloads");
        Check(ExceptionAfterUnload(0x6F4) && ExceptionAfterUnload(EXCEPTION_ACCESS_VIOLATION),
            "exceptions after scan unload do not call stale plugin handlers");
    }
    std::puts("Plugin lifecycle and 50 temporary-load cycles passed");
}
