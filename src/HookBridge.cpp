#include "HookBridge.h"

#include "DesignerLog.h"

#include <Windows.h>

#include <string>

namespace {

using GenerateAssemblyFn = BOOL(WINAPI*)(const char*, const char*, const char*);
using InsertAnsiFn = BOOL(WINAPI*)(const char*);
using LastReasonFn = const char*(WINAPI*)();

std::wstring HookPath()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&HookPath), &module);
    wchar_t path[MAX_PATH]{};
    if (module == nullptr || GetModuleFileNameW(module, path, MAX_PATH) == 0) {
        return {};
    }
    std::wstring result(path);
    const size_t slash = result.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    result.resize(slash + 1);
    result += L"jadehook.dll";
    return result;
}

GenerateAssemblyFn ResolveGenerate(std::string& error)
{
    static HMODULE hookModule = nullptr;
    static GenerateAssemblyFn generate = nullptr;
    if (generate != nullptr) return generate;
    const std::wstring path = HookPath();
    if (path.empty()) {
        error = "hook path unavailable";
        return nullptr;
    }
    hookModule = LoadLibraryW(path.c_str());
    if (hookModule == nullptr) {
        error = "jadehook.dll load failed: " + std::to_string(GetLastError());
        return nullptr;
    }
    generate = reinterpret_cast<GenerateAssemblyFn>(
        GetProcAddress(hookModule, "JadeHookGenerateAssembly"));
    if (generate == nullptr) {
        generate = reinterpret_cast<GenerateAssemblyFn>(
            GetProcAddress(hookModule, "_JadeHookGenerateAssembly@12"));
    }
    if (generate == nullptr) {
        error = "JadeHookGenerateAssembly export missing";
    }
    return generate;
}

} // namespace

namespace HookBridge {

bool GenerateAssembly(
    const std::string& assemblyAnsi,
    const std::string& channelAnsi,
    const std::string& handlerAnsi,
    std::string& error)
{
    GenerateAssemblyFn generate = ResolveGenerate(error);
    if (generate == nullptr) return false;
    const BOOL result = generate(
        assemblyAnsi.c_str(), channelAnsi.c_str(), handlerAnsi.c_str());
    DesignerLog::Write(
        "HYBRID hook_generate result=" + std::to_string(result ? 1 : 0) +
        " assembly=\"" + DesignerLog::ToUtf8(std::wstring(
            assemblyAnsi.begin(), assemblyAnsi.end())) + "\"");
    if (!result && error.empty()) error = "hook generate returned false";
    return result != FALSE;
}

bool InsertAnsi(const std::string& text, std::string& error)
{
    const std::wstring path = HookPath();
    if (path.empty()) {
        error = "hook path unavailable";
        return false;
    }
    HMODULE module = LoadLibraryW(path.c_str());
    if (module == nullptr) {
        error = "jadehook.dll load failed: " + std::to_string(GetLastError());
        return false;
    }
    auto insert = reinterpret_cast<InsertAnsiFn>(GetProcAddress(module, "JadeHookInsertAnsi"));
    if (insert == nullptr) {
        insert = reinterpret_cast<InsertAnsiFn>(GetProcAddress(module, "_JadeHookInsertAnsi@4"));
    }
    const bool ok = insert != nullptr && insert(text.c_str()) != FALSE;
    if (!ok) {
        auto reason = reinterpret_cast<LastReasonFn>(GetProcAddress(module, "JadeHookLastReason"));
        if (reason == nullptr) {
            reason = reinterpret_cast<LastReasonFn>(GetProcAddress(module, "_JadeHookLastReason@0"));
        }
        error = reason != nullptr ? reason() : "hook insert returned false";
    }
    FreeLibrary(module);
    return ok;
}

} // namespace HookBridge
