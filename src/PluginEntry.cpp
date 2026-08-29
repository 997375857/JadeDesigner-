#include <Windows.h>

#include <atomic>
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

void EnsureModulesInitialized()
{
    if (g_modulesInitialized) {
        return;
    }
    DesignerLog::Initialize(g_module);
    DesignerLog::ResetForSession();
    IDEIntegration::InitializeModule(g_module);
    g_modulesInitialized = true;
}

bool AttachToIde()
{
    EnsureModulesInitialized();
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
    info.m_nBuildNumber = 5;
    info.m_nRqSysMajorVer = 3;
    info.m_nRqSysMinorVer = 7;
    info.m_nRqSysKrnlLibMajorVer = 5;
    info.m_nRqSysKrnlLibMinorVer = 3;
    info.m_szName = "JadeHybrid";
    info.m_nLanguage = __GBK_LANG_VER;
    info.m_szExplain = "JadeDesigner hybrid hook experiment for E-language IDE 5.95.";
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
        IDEIntegration::Stop();
        return NR_OK;
    }
    return baseResult;
}

extern "C" __declspec(dllexport) PLIB_INFOX WINAPI GetNewInf()
{
    EnsureModulesInitialized();
    DesignerLog::Write("GetNewInf called; JadeHybrid 0.3 (HOOK_FIXED_APPEND)");
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
