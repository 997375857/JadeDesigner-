#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>

#include <fnshare.h>
#include <lang.h>
#include <lib2.h>

#include "DesignerLog.h"
#include "IDEIntegration.h"

namespace {

constexpr char kNotifyFunctionName[] = "JadeDesigner_MessageNotify";
constexpr char kAddInInfo[] =
    "Toggle Jade Preview\0"
    "Show or hide the JadeDesigner WebView2 preview.\0"
    "Refresh Jade Preview\0"
    "Reload JadeDesigner\\web\\index.html.\0"
    "\0";

HMODULE g_module = nullptr;
HWND g_mainWindow = nullptr;
std::atomic_bool g_notifySystemReady = false;
bool g_modulesInitialized = false;
PVOID g_faultProbe = nullptr;
volatile LONG g_faultsReported = 0;

// A fault inside e5.95 dies quietly: the IDE installs its own handler, so
// Windows records neither an Application Error event nor a crash dump, and the
// plugin log simply stops mid-operation with no indication of whose code was
// running. A vectored handler runs ahead of every SEH frame, so it can name the
// faulting address and the module that owns it while the process is still
// alive. It only reports - returning EXCEPTION_CONTINUE_SEARCH leaves the
// outcome exactly as it would have been without this handler.
LONG CALLBACK ReportFault(EXCEPTION_POINTERS* pointers)
{
    if (pointers == nullptr || pointers->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
    switch (record.ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
        break;
    default:
        // C++ throws and debugger notifications are ordinary traffic here, and
        // a stack overflow leaves too little room to format a line safely.
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // A fault that repeats must not turn the log into a flood.
    if (InterlockedIncrement(&g_faultsReported) > 16) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const void* address = record.ExceptionAddress;
    char moduleName[MAX_PATH] = "?";
    std::uintptr_t offset = 0;
    HMODULE owner = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(address), &owner) &&
        owner != nullptr) {
        if (GetModuleFileNameA(owner, moduleName, static_cast<DWORD>(sizeof(moduleName))) == 0) {
            moduleName[0] = '?';
            moduleName[1] = '\0';
        }
        offset = reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(owner);
    }

    const char* operation = "-";
    std::uintptr_t touched = 0;
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
        switch (record.ExceptionInformation[0]) {
        case 0: operation = "read"; break;
        case 1: operation = "write"; break;
        case 8: operation = "execute"; break;
        default: break;
        }
        touched = static_cast<std::uintptr_t>(record.ExceptionInformation[1]);
    }

    char line[MAX_PATH + 256];
    _snprintf_s(
        line, sizeof(line), _TRUNCATE,
        "CRASH exception=0x%08lX op=%s at=0x%08zX module=\"%s\"+0x%zX touched=0x%08zX thread=%lu",
        static_cast<unsigned long>(record.ExceptionCode), operation,
        reinterpret_cast<std::uintptr_t>(address), moduleName, offset, touched,
        GetCurrentThreadId());
    DesignerLog::WriteFromFault(line);
    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallFaultProbe()
{
    if (g_faultProbe == nullptr) {
        g_faultProbe = AddVectoredExceptionHandler(1, ReportFault);
    }
}

void RemoveFaultProbe()
{
    if (g_faultProbe != nullptr) {
        RemoveVectoredExceptionHandler(g_faultProbe);
        g_faultProbe = nullptr;
    }
}

void EnsureModulesInitialized()
{
    if (g_modulesInitialized) {
        return;
    }
    DesignerLog::Initialize(g_module);
    InstallFaultProbe();
    IDEIntegration::InitializeModule(g_module);
    g_modulesInitialized = true;
}

bool AttachToIde()
{
    EnsureModulesInitialized();
    // NL_IDE_READY arrives again after the user re-selects the library, so the
    // probe has to be re-armed here rather than only on first initialization.
    InstallFaultProbe();
    if (!g_notifySystemReady) {
        DesignerLog::Write("PLUGIN attach deferred: NotifySys is not ready");
        return false;
    }
    g_mainWindow = reinterpret_cast<HWND>(NotifySys(NES_GET_MAIN_HWND, 0, 0));
    DesignerLog::Write(
        "PLUGIN main_window=" + DesignerLog::HexPointer(g_mainWindow) +
        " valid=" + std::to_string(g_mainWindow != nullptr && IsWindow(g_mainWindow) ? 1 : 0));
    return IDEIntegration::Start(g_mainWindow);
}

INT WINAPI RunAddInFunction(INT index)
{
    DesignerLog::Write("PLUGIN addin index=" + std::to_string(index));
    AttachToIde();
    if (index == 0) {
        IDEIntegration::TogglePreview();
    }
    else if (index == 1) {
        IDEIntegration::RefreshPreview();
    }
    return 0;
}

LIB_INFOX BuildLibraryInfo();
LIB_INFOX g_libraryInfo = BuildLibraryInfo();

LIB_INFOX BuildLibraryInfo()
{
    LIB_INFOX info{};
    info.m_dwLibFormatVer = LIB_FORMAT_VER;
    info.m_szGuid = "BB5D8F7A-0E32-4B13-A53B-5129F51AB0C4";
    info.m_nMajorVersion = 1;
    info.m_nMinorVersion = 6;
    info.m_nBuildNumber = 11;
    info.m_nRqSysMajorVer = 3;
    info.m_nRqSysMinorVer = 7;
    info.m_nRqSysKrnlLibMajorVer = 5;
    info.m_nRqSysKrnlLibMinorVer = 3;
    info.m_szName = "JadeHybrid";
    info.m_nLanguage = __GBK_LANG_VER;
    info.m_szExplain = "JadeDesigner in-memory bridge version for E-language IDE 5.95.";
    info.m_dwState = LBS_IDE_PLUGIN | LBS_LIB_INFO2 | LBS_FUNC_NO_RUN_CODE;
    info.m_szAuthor = "JadeDesigner";
    info.m_nDataTypeCount = 0;
    info.m_pDataType = nullptr;
    info.m_nCategoryCount = 0;
    info.m_szzCategory = nullptr;
    info.m_nCmdCount = 0;
    info.m_pBeginCmdInfo = nullptr;
    info.m_pCmdsFunc = nullptr;
    info.m_pfnRunAddInFn = RunAddInFunction;
    info.m_szzAddInFnInfo = kAddInInfo;
    info.m_pfnNotify = nullptr;
    info.m_pfnSuperTemplate = nullptr;
    info.m_szzSuperTemplateInfo = nullptr;
    info.m_nLibConstCount = 0;
    info.m_pLibConst = nullptr;
    info.m_szzDependFiles = nullptr;
    info.m_szHardwareCode = nullptr;
    info.m_szBuyingTips = nullptr;
    info.m_szBuyingURL = nullptr;
    info.m_szLicenseToUserName = nullptr;
    return info;
}

} // namespace

extern "C" INT WINAPI JadeDesigner_MessageNotify(INT message, DWORD parameter1, DWORD parameter2)
{
    EnsureModulesInitialized();
    std::ostringstream stream;
    stream << "PLUGIN notify message=" << message
           << " parameter1=0x" << std::uppercase << std::hex << parameter1
           << " parameter2=0x" << parameter2;
    DesignerLog::Write(stream.str());

    if (message == NL_GET_CMD_FUNC_NAMES) {
        return 0;
    }
    if (message == NL_GET_NOTIFY_LIB_FUNC_NAME) {
        return reinterpret_cast<INT>(kNotifyFunctionName);
    }
    if (message == NL_GET_DEPENDENT_LIBS) {
        return 0;
    }

    const INT baseResult = ProcessNotifyLib(message, parameter1, parameter2);
    if (message == NL_SYS_NOTIFY_FUNCTION) {
        g_notifySystemReady = parameter1 != 0;
        DesignerLog::Write(g_notifySystemReady ? "PLUGIN NotifySys ready" : "PLUGIN NotifySys missing");
        return baseResult;
    }
    if (message == NL_IDE_READY) {
        AttachToIde();
        return NR_OK;
    }
    if (message == NL_UNLOAD_FROM_IDE || message == NL_FREE_LIB_DATA) {
        DesignerLog::Write("PLUGIN shutdown notification");
        g_notifySystemReady = false;
        g_mainWindow = nullptr;
        // The handler lives in this module; leaving it registered past unload
        // would turn the next exception anywhere in the IDE into a jump into
        // freed code.
        RemoveFaultProbe();
        IDEIntegration::Stop();
        return NR_OK;
    }
    return baseResult;
}

extern "C" __declspec(dllexport) PLIB_INFOX WINAPI GetNewInf()
{
    EnsureModulesInitialized();
    DesignerLog::Write("GetNewInf called; JadeHybrid 1.0 (IN_MEMORY_BRIDGE_QUEUED)");
    g_libraryInfo.m_pfnNotify = JadeDesigner_MessageNotify;
    return &g_libraryInfo;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
