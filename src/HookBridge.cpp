#include "HookBridge.h"

#include "DesignerLog.h"

#include <Windows.h>

#include <cstring>
#include <string>

namespace {

using GenerateAssemblyFn = BOOL(WINAPI*)(const char*, const char*, const char*);
using InsertAnsiFn = BOOL(WINAPI*)(const char*);
using ReadPageCodeFn = int(WINAPI*)(char*, int);
using LastReasonFn = const char*(WINAPI*)();
constexpr UINT kIdeCodePage = 936;
// The only host the hook accepts, and the first bytes of the editor's '.'
// keyboard handler at 0x004C2290 in that build - the routine every hook path
// goes through. A same-named executable from a different build fails this even
// though it passes the name check.
constexpr wchar_t kSupportedHostExe[] = L"e5.95.exe";
constexpr DWORD kEditorInsertHandlerRva = 0xC2290;
constexpr unsigned char kEditorInsertHandlerSignature[] = {
    0x8B, 0x44, 0x24, 0x04, 0x56, 0x66, 0x3D, 0x2E, 0x00};
// Big enough for any realistic assembly page, so the size-probe retry below is
// the exception rather than the rule.
constexpr int kInitialPageBufferBytes = 64 * 1024;

// The IDE's strings are GBK. Widening them byte-by-byte splits every Chinese
// character in two, so the log has to go through the code page properly.
std::string AnsiToUtf8(const std::string& ansi)
{
    if (ansi.empty()) return {};
    const int required = MultiByteToWideChar(
        kIdeCodePage, 0, ansi.data(), static_cast<int>(ansi.size()), nullptr, 0);
    if (required <= 0) return {};
    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(
        kIdeCodePage, 0, ansi.data(), static_cast<int>(ansi.size()), wide.data(), required);
    return DesignerLog::ToUtf8(wide);
}

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

// The hook is built without a .def file, so its __stdcall exports carry the
// decorated name. Ask for the plain one first and fall back to the decorated
// form.
FARPROC ResolveExport(HMODULE module, const char* plain, const char* decorated)
{
    FARPROC address = GetProcAddress(module, plain);
    return address != nullptr ? address : GetProcAddress(module, decorated);
}

// Kept loaded for the life of the IDE session. The hook holds the editor state
// the router depends on, so unloading it between calls would throw that away.
HMODULE HookModule(std::string& error)
{
    static HMODULE cached = nullptr;
    static bool checked = false;
    static bool compatible = false;
    if (checked) {
        if (!compatible) error = "0908 jadehook.dll requires background-memory API v3; replace both binaries";
        return compatible ? cached : nullptr;
    }
    const std::wstring path = HookPath();
    if (path.empty()) {
        error = "hook path unavailable";
        return nullptr;
    }
    cached = LoadLibraryW(path.c_str());
    if (cached == nullptr) {
        error = "jadehook.dll load failed: " + std::to_string(GetLastError());
        return nullptr;
    }
    using VersionFn = DWORD(WINAPI*)();
    auto version = reinterpret_cast<VersionFn>(
        ResolveExport(cached, "JadeHookMemoryApiVersion", "_JadeHookMemoryApiVersion@0"));
    compatible = version != nullptr && version() == 3;
    checked = true;
    if (!compatible) {
        // Old hook builds own a worker thread. Do not unload code it may execute.
        error = "0908 jadehook.dll requires background-memory API v3; replace both binaries";
        return nullptr;
    }
    DesignerLog::Write("BACKGROUND bridge_api=3 transport=project_memory_model");
    return cached;
}

std::string LastReason(HMODULE module, const char* fallback)
{
    auto reason = reinterpret_cast<LastReasonFn>(
        ResolveExport(module, "JadeHookLastReason", "_JadeHookLastReason@0"));
    return reason != nullptr ? reason() : fallback;
}

GenerateAssemblyFn ResolveGenerate(std::string& error)
{
    static GenerateAssemblyFn generate = nullptr;
    if (generate != nullptr) return generate;
    HMODULE module = HookModule(error);
    if (module == nullptr) return nullptr;
    generate = reinterpret_cast<GenerateAssemblyFn>(
        ResolveExport(module, "JadeHookGenerateAssembly", "_JadeHookGenerateAssembly@12"));
    if (generate == nullptr) {
        error = "JadeHookGenerateAssembly export missing";
    }
    return generate;
}

} // namespace

namespace HookBridge {

int EnsureBackground(const std::string& callbackAssembly, const std::string& assembly, const std::string& fixed,
    const std::string& handler, const std::string& statement,
    const std::string& callback, std::string& error, BackgroundChange& change)
{
    change = BackgroundChange::Unknown;
    HMODULE module = HookModule(error);
    if (!module) return -1;
    using Fn = int(WINAPI*)(const char*,const char*,const char*,const char*,const char*,const char*);
    auto ensure = reinterpret_cast<Fn>(ResolveExport(module,
        "JadeHookEnsureBackgroundAnsi", "_JadeHookEnsureBackgroundAnsi@24"));
    if (!ensure) { error = "background memory export missing"; return -1; }
    const int result = ensure(callbackAssembly.c_str(),assembly.c_str(),fixed.c_str(),handler.c_str(),statement.c_str(),callback.c_str());
    const std::string reason = LastReason(module,"background operation failed");
    if (result != 1 && result != 2) error = reason;
    if (result == 1 && reason == "background_callback_created_verified") {
        change = BackgroundChange::CallbackCreated;
    } else if (result == 1 && reason == "background_subscription_repaired_verified") {
        change = BackgroundChange::SubscriptionRepaired;
    }
    DesignerLog::Write("BACKGROUND ensure result=" + std::to_string(result) +
        " assembly=\"" + AnsiToUtf8(assembly) + "\" handler=\"" + AnsiToUtf8(handler) +
        "\" reason=\"" + reason + "\"");
    return result;
}

HostInfo InspectHost()
{
    HostInfo info;
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return info;
    }
    std::wstring exeName(path);
    const size_t slash = exeName.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        exeName.erase(0, slash + 1);
    }
    info.exeNameUtf8 = DesignerLog::ToUtf8(exeName);
    info.nameSupported = _wcsicmp(exeName.c_str(), kSupportedHostExe) == 0;

    const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (base == nullptr) {
        return info;
    }
    // Reading the signature blind would fault inside a host whose image is
    // smaller than the RVA, so bound it by the image the loader actually mapped.
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return info;
    }
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        kEditorInsertHandlerRva + sizeof(kEditorInsertHandlerSignature) >
            nt->OptionalHeader.SizeOfImage) {
        return info;
    }
    info.buildSupported =
        memcmp(
            base + kEditorInsertHandlerRva,
            kEditorInsertHandlerSignature,
            sizeof(kEditorInsertHandlerSignature)) == 0;
    return info;
}

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
        " assembly=\"" + AnsiToUtf8(assemblyAnsi) + "\"");
    if (!result && error.empty()) error = LastReason(HookModule(error), "hook generate returned false");
    return result != FALSE;
}

bool InsertAnsi(const std::string& text, std::string& error)
{
    HMODULE module = HookModule(error);
    if (module == nullptr) return false;
    auto insert = reinterpret_cast<InsertAnsiFn>(
        ResolveExport(module, "JadeHookInsertAnsi", "_JadeHookInsertAnsi@4"));
    const bool ok = insert != nullptr && insert(text.c_str()) != FALSE;
    if (!ok) {
        error = LastReason(module, "hook insert returned false");
    }
    return ok;
}

bool ReadPageCode(std::string& pageUtf8, std::string& error)
{
    pageUtf8.clear();
    HMODULE module = HookModule(error);
    if (module == nullptr) return false;
    auto read = reinterpret_cast<ReadPageCodeFn>(
        ResolveExport(module, "JadeHookReadPageCode", "_JadeHookReadPageCode@8"));
    if (read == nullptr) {
        error = "JadeHookReadPageCode export missing";
        return false;
    }
    // The hook reports the length it needs when the buffer is too small, so an
    // unusually large page costs one extra call rather than a truncated answer -
    // and a truncated page would read as "this subroutine is missing".
    std::string buffer(kInitialPageBufferBytes, '\0');
    for (int attempt = 0; attempt < 2; ++attempt) {
        const int length = read(buffer.data(), static_cast<int>(buffer.size()));
        if (length < 0) {
            error = LastReason(module, "hook page read returned failure");
            return false;
        }
        if (static_cast<size_t>(length) < buffer.size()) {
            buffer.resize(static_cast<size_t>(length));
            pageUtf8.swap(buffer);
            return true;
        }
        buffer.assign(static_cast<size_t>(length) + 1, '\0');
    }
    error = "page code kept growing between reads";
    return false;
}

} // namespace HookBridge
