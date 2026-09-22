#include <Windows.h>

#include <atomic>
#include <string>

#include <fnshare.h>
#include <lang.h>
#include <lib2.h>

#include "DesignerLog.h"
#include "IDEIntegration.h"

extern "C" INT WINAPI JadeDesigner_MessageNotify(INT message, DWORD parameter1, DWORD parameter2);

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

void EnsureModulesInitialized()
{
    if (g_modulesInitialized) {
        return;
    }
    DesignerLog::Initialize(g_module);
    IDEIntegration::InitializeModule(g_module);
    g_modulesInitialized = true;
}

bool AttachToIde()
{
    if (!g_notifySystemReady) {
        return false;
    }
    EnsureModulesInitialized();
    g_mainWindow = reinterpret_cast<HWND>(NotifySys(NES_GET_MAIN_HWND, 0, 0));
    DesignerLog::Write(
        "PLUGIN main_window=" + DesignerLog::HexPointer(g_mainWindow) +
        " valid=" + std::to_string(g_mainWindow != nullptr && IsWindow(g_mainWindow) ? 1 : 0));
    return IDEIntegration::Start(g_mainWindow);
}

INT WINAPI RunAddInFunction(INT index)
{
    if (!AttachToIde()) return NR_ERR;
    DesignerLog::Write("PLUGIN addin index=" + std::to_string(index));
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
    info.m_pfnNotify = JadeDesigner_MessageNotify;
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
        return baseResult;
    }
    if (message == NL_IDE_READY) {
        return AttachToIde() ? NR_OK : NR_ERR;
    }
    if (message == NL_UNLOAD_FROM_IDE || message == NL_FREE_LIB_DATA) {
        if (g_modulesInitialized) {
            DesignerLog::Write("PLUGIN shutdown notification");
            IDEIntegration::Stop();
            g_modulesInitialized = false;
        }
        g_notifySystemReady = false;
        g_mainWindow = nullptr;
        ProcessNotifyLib(NL_SYS_NOTIFY_FUNCTION, 0, 0);
        return NR_OK;
    }
    return baseResult;
}

extern "C" __declspec(dllexport) PLIB_INFOX WINAPI GetNewInf()
{
    // The configuration dialog also calls this during temporary library scans.
    // It must be safe to FreeLibrary immediately, without lifecycle notices.
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
