#include <Windows.h>
#include <cstdio>
#include <cstring>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) return 3;
    auto version = reinterpret_cast<DWORD(WINAPI*)()>(GetProcAddress(module, "_JadeHookMemoryApiVersion@0"));
    auto probe = reinterpret_cast<BOOL(WINAPI*)()>(GetProcAddress(module, "_JadeHookProbe@0"));
    auto read = reinterpret_cast<int(WINAPI*)(char*, int)>(GetProcAddress(module, "_JadeHookReadPageCode@8"));
    auto write = reinterpret_cast<BOOL(WINAPI*)(const char*)>(GetProcAddress(module, "_JadeHookInsertAnsi@4"));
    auto generate = reinterpret_cast<BOOL(WINAPI*)(const char*, const char*, const char*)>(GetProcAddress(module, "_JadeHookGenerateAssembly@12"));
    auto reason = reinterpret_cast<const char*(WINAPI*)()>(GetProcAddress(module, "_JadeHookLastReason@0"));
    auto readAssembly = reinterpret_cast<int(WINAPI*)(const char*,char*,int)>(GetProcAddress(module,"_JadeHookReadAssemblyCode@12"));
    auto ensure = reinterpret_cast<int(WINAPI*)(const char*,const char*,const char*,const char*,const char*,const char*)>(GetProcAddress(module,"_JadeHookEnsureBackgroundAnsi@24"));
    if (!version || !probe || !read || !write || !generate || !reason || !readAssembly || !ensure) return 4;
    char buffer[32] = "untouched";
    const bool ok = version() == 3 && !probe() && read(buffer, sizeof(buffer)) == -1 &&
        !std::strcmp(buffer, "untouched") && !write(".sub\r\n") &&
        !generate("test", "test", "test") &&
        readAssembly("test",buffer,sizeof(buffer)) == -1 && !std::strcmp(buffer,"untouched") &&
        ensure("test","test","fixed","handler","statement","callback") == -1 &&
        !std::strcmp(reason(), "memory_host_fingerprint_mismatch");
    FreeLibrary(module);
    std::puts(ok ? "Memory bridge API and unsupported-host guards passed" : "FAILED memory bridge contract");
    return ok ? 0 : 1;
}
