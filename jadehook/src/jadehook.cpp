#include <Windows.h>
#include <wincrypt.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#pragma comment(lib, "advapi32.lib")

namespace {
constexpr size_t kMaxSource = 16 * 1024 * 1024;
char g_reason[96] = "not_run";
LONG g_busy = 0;
template<class T> T At(uintptr_t va) {
    return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + va - 0x400000);
}
void Reason(const char* text) {
    strncpy_s(g_reason, text, _TRUNCATE);
    OutputDebugStringA("jadehook: "); OutputDebugStringA(text); OutputDebugStringA("\n");
}
bool SupportedHost() {
    // The layouts below are specific to the exact analyzed executable.
    static const bool matched = [] {
        wchar_t path[MAX_PATH]{};
        DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (!n || n >= MAX_PATH) return false;
        const wchar_t* name = wcsrchr(path, L'\\');
        if (_wcsicmp(name ? name + 1 : path, L"e5.95.exe")) return false;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
        bool ok = CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE;
        if (ok) ok = CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
        BYTE chunk[16384]{}; DWORD count = 0;
        while (ok) {
            if (!ReadFile(file, chunk, sizeof(chunk), &count, nullptr)) { ok = false; break; }
            if (!count) break;
            ok = CryptHashData(hash, chunk, count, 0) != FALSE;
        }
        BYTE digest[32]{}; DWORD size = sizeof(digest);
        if (ok) ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0) != FALSE;
        constexpr BYTE expected[] = {
            0x36,0x8C,0xBB,0xD3,0x23,0xD2,0xC5,0xBC,0x00,0xF0,0xC0,0x72,0xB1,0x16,0x33,0x3F,
            0x75,0x33,0x9F,0xC0,0x74,0x2E,0x23,0x8D,0x3E,0x5E,0x6F,0xF1,0x7A,0xBE,0x14,0x09};
        ok = ok && size == sizeof(expected) && !memcmp(digest, expected, size);
        if (hash) CryptDestroyHash(hash);
        if (provider) CryptReleaseContext(provider, 0);
        CloseHandle(file);
        return ok;
    }();
    return matched;
}
HWND MainWindow() {
    HWND found = nullptr;
    EnumWindows([](HWND window, LPARAM arg) -> BOOL {
        DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
        if (pid == GetCurrentProcessId() && FindWindowExA(window, nullptr, "MDIClient", nullptr)) {
            *reinterpret_cast<HWND*>(arg) = window; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}
struct Operation {
    bool entered = false;
    Operation() {
        HWND main = MainWindow();
        if (!SupportedHost()) Reason("memory_host_fingerprint_mismatch");
        else if (!main || GetWindowThreadProcessId(main, nullptr) != GetCurrentThreadId())
            Reason("memory_io_requires_ide_thread");
        else if (InterlockedCompareExchange(&g_busy, 1, 0)) Reason("memory_io_reentrant_call_refused");
        else entered = true;
    }
    ~Operation() { if (entered) InterlockedExchange(&g_busy, 0); }
};
// Native allocations belong to the IDE heap, not this DLL's CRT.
struct Buffer {
    uintptr_t words[5]{};
    Buffer() { At<void(__thiscall*)(void*)>(0x4918B0)(words); }
    ~Buffer() { At<void(__thiscall*)(void*)>(0x491AB0)(words); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    const char* data() const { return reinterpret_cast<const char*>(words[2]); }
    size_t size() const { return words[4]; }
};
struct Package {
    alignas(4) BYTE bytes[0x36C]{};
    Package() { At<void(__thiscall*)(void*)>(0x40E5E0)(bytes); }
    ~Package() { At<void(__thiscall*)(void*)>(0x401F70)(bytes); }
    Package(const Package&) = delete;
    Package& operator=(const Package&) = delete;
};
uintptr_t Editor() {
    uintptr_t editor = *At<uintptr_t*>(0x675B44);
    if (!editor || *reinterpret_cast<int*>(editor + 0x3C) != 1) {
        Reason("memory_active_assembly_editor_missing"); return 0;
    }
    return editor;
}
bool Serialize(uintptr_t editor, std::string& ansi) {
    int rows = At<int(__thiscall*)(uintptr_t)>(0x4CCCB0)(editor);
    if (rows <= 0 || rows > 1000000) { Reason("memory_model_row_count_invalid"); return false; }
    Buffer text;
    // 4D48D9 reads the buffer from [esp+arg_0], and 4D4909 returns without
    // popping it. Despite the decompiler's inferred type this is CDECL.
    At<void(__cdecl*)(void*)>(0x4D48A0)(text.words);
    // Source serializer underneath copy, not the copy command: full model range.
    At<void(__thiscall*)(uintptr_t, int, int, void*, int)>(0x49A140)(
        editor, 0, rows - 1, text.words, 0);
    if (!text.data() || !text.size() || text.size() > kMaxSource) {
        Reason("memory_source_buffer_invalid"); return false;
    }
    ansi.assign(text.data(), text.size());
    while (!ansi.empty() && !ansi.back()) ansi.pop_back();
    return !ansi.empty();
}
bool Utf8(const std::string& ansi, std::string& result) {
    int n = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, ansi.data(),
        static_cast<int>(ansi.size()), nullptr, 0);
    if (n <= 0) return false;
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, ansi.data(), static_cast<int>(ansi.size()), wide.data(), n);
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), n, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return false;
    result.resize(static_cast<size_t>(size));
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), n,
        result.data(), size, nullptr, nullptr) == size;
}
int Command(uintptr_t editor, int code, int mode, uintptr_t a, uintptr_t b) {
    using Fn = int(__thiscall*)(uintptr_t, int, int, uintptr_t, uintptr_t);
    uintptr_t table = *reinterpret_cast<uintptr_t*>(editor);
    return (*reinterpret_cast<Fn*>(table + 0xE0))(editor, code, mode, a, b);
}
bool ParseAndCommit(uintptr_t editor, const char* source, size_t length) {
    Buffer text; Package package;
    At<void(__thiscall*)(void*, const void*, int)>(0x492170)(text.words, source, static_cast<int>(length));
    At<void(__thiscall*)(void*, int)>(0x491BB0)(text.words, 0);
    // ECX survives 4D59B0 into 4D78F0: it is the package, NOT the real editor.
    if (!At<int(__thiscall*)(void*, void*)>(0x4D59B0)(package.bytes, text.words)) {
        Reason("memory_source_parse_failed"); return false;
    }
    // Match the native parser's ownership transfer (4C8B53..4C8C24).
    int libraries = *reinterpret_cast<int*>(package.bytes + 0x100);
    auto names = *reinterpret_cast<const char***>(package.bytes + 0xFC);
    for (int i = 0; i < libraries; ++i)
        At<int(__cdecl*)(void*, const char*, int)>(0x501270)(At<void*>(0x675898), names[i], 0);
    int count = *reinterpret_cast<int*>(package.bytes + 0x1FC);
    for (int i = count - 1; i >= 0; --i) {
        auto items = *reinterpret_cast<BYTE***>(package.bytes + 0x1F8);
        BYTE* item = items[i];
        for (int offset : {0x38, 0x18})
            At<void(__thiscall*)(void*, void*, int, int)>(0x4E7380)(
                package.bytes + 0x244, item + offset, 0, *reinterpret_cast<int*>(item + offset + 4));
        if (*reinterpret_cast<int*>(item + 0xCC)) {
            for (int offset : {0x38, 0x18}) {
                using Clear = void(__thiscall*)(void*);
                auto table = *reinterpret_cast<uintptr_t*>(item + offset);
                (*reinterpret_cast<Clear*>(table + 8))(item + offset);
            }
        } else {
            At<void(__thiscall*)(void*)>(0x40F4E0)(item);
            At<void(__cdecl*)(void*)>(0x5BD402)(item);
            At<void(__thiscall*)(void*, int, int)>(0x5BB445)(package.bytes + 0x1F4, i, 1);
        }
    }
    // The normal command wrapper opens/closes the undo transaction around the
    // model commit. Calling only the commit leaves its undo records ungrouped.
    Buffer undoState;
    const bool ownsUndo = *At<int*>(0x67625C) == -1;
    using Capture = void(__thiscall*)(uintptr_t, void*);
    const auto capture = *reinterpret_cast<Capture*>(
        *reinterpret_cast<uintptr_t*>(editor) + 0xD0);
    if (ownsUndo) {
        capture(editor, undoState.words);
        At<void(__thiscall*)(void*, void*)>(0x50BE60)(At<void*>(0x676258), undoState.words);
    }
    // The commit's THIS is the real editor, not the temporary package.
    At<void(__thiscall*)(uintptr_t, void*, int, int, int)>(0x496E40)(editor, package.bytes, 0, 0, 1);
    if (ownsUndo) {
        capture(editor, undoState.words);
        At<void(__thiscall*)(void*, void*)>(0x50BF30)(At<void*>(0x676258), undoState.words);
    }
    return true;
}
#include "background_model.inl"
#include "common_model.inl"
} // namespace
extern "C" __declspec(dllexport) int WINAPI JadeHookEnsureCommonAnsi(
    const char* name, const char* source, const char* subscription, const char* fixed) {
    Operation op; if (!op.entered) return -1;
    return EnsureCommon(name,source,subscription,fixed);
}
extern "C" __declspec(dllexport) DWORD WINAPI JadeHookMemoryApiVersion() { return 3; }
extern "C" __declspec(dllexport) int WINAPI JadeHookEnsureBackgroundAnsi(
    const char* callbackAssembly, const char* assembly, const char* fixed, const char* handler, const char* statement, const char* callback) {
    Operation op; if (!op.entered) return -1;
    return EnsureBackground(callbackAssembly, assembly, fixed, handler, statement, callback);
}
extern "C" __declspec(dllexport) int WINAPI JadeHookReadAssemblyCode(const char* name, char* buffer, int capacity) {
    Operation op; if (!op.entered) return -1;
    if (!name || !*name) { Reason("background_invalid_assembly_name"); return -1; }
    ModelItem assembly;
    const int copies = FindModel(Project() + 640, name, assembly);
    if (!copies) { Reason("background_assembly_absent"); return 0; }
    if (copies != 1) { Reason("background_model_ambiguous"); return -1; }
    std::string ansi, utf8;
    if (!SerializeAssembly(Project(),assembly,ansi) || !Utf8(ansi,utf8)) {
        Reason("background_read_failed"); return -1;
    }
    const int length = static_cast<int>(utf8.size());
    if (!buffer || capacity <= length) return length;
    memcpy(buffer,utf8.c_str(),utf8.size()+1);
    Reason("background_model_read_success"); return length;
}
extern "C" __declspec(dllexport) int WINAPI JadeHookReadRoutineCode(const char* name, char* buffer, int capacity) {
    Operation op; if (!op.entered) return -1;
    if (!name || !*name) { Reason("health_invalid_routine_name"); return -1; }
    ModelItem routine;
    const int copies=FindModel(Project()+612,name,routine);
    if (!copies) { Reason("health_routine_absent"); return 0; }
    if (copies!=1) { Reason("health_routine_ambiguous"); return -1; }
    Buffer text; std::string ansi,utf8;
    SerializeSub(routine,text);
    if (!BufferString(text,ansi) || !Utf8(ansi,utf8)) { Reason("health_routine_read_failed"); return -1; }
    const int length=static_cast<int>(utf8.size());
    if (!buffer || capacity<=length) return length;
    memcpy(buffer,utf8.c_str(),utf8.size()+1);
    Reason("health_routine_read_success"); return length;
}
extern "C" __declspec(dllexport) const char* WINAPI JadeHookLastReason() { return g_reason; }
extern "C" __declspec(dllexport) BOOL WINAPI JadeHookProbe() { return SupportedHost(); }
extern "C" __declspec(dllexport) int WINAPI JadeHookReadPageCode(char* buffer, int capacity) {
    Operation op; if (!op.entered) return -1;
    uintptr_t editor = Editor(); if (!editor) return -1;
    int row = *reinterpret_cast<int*>(editor + 0x74), column = *reinterpret_cast<int*>(editor + 0x78);
    std::string ansi, utf8;
    if (!Serialize(editor, ansi)) return -1;
    if (!Utf8(ansi, utf8)) { Reason("memory_source_encoding_invalid"); return -1; }
    if (*reinterpret_cast<int*>(editor + 0x74) != row || *reinterpret_cast<int*>(editor + 0x78) != column) {
        Reason("memory_read_unexpected_caret_change"); return -1;
    }
    int length = static_cast<int>(utf8.size());
    if (!buffer || capacity <= length) { Reason("memory_read_buffer_too_small"); return length; }
    memcpy(buffer, utf8.c_str(), utf8.size() + 1);
    Reason("memory_model_read_success"); return length;
}
extern "C" __declspec(dllexport) BOOL WINAPI JadeHookInsertAnsi(const char* text) {
    Operation op; if (!op.entered) return FALSE;
    uintptr_t editor = Editor(); if (!editor) return FALSE;
    const auto project = *reinterpret_cast<uintptr_t*>(editor + 0x5C);
    if (!project || *reinterpret_cast<int*>(project + 852) == 1 ||
        *reinterpret_cast<int*>(project + 848)) {
        Reason("memory_project_not_editable"); return FALSE;
    }
    if (!text || !*text || strnlen_s(text, kMaxSource + 1) > kMaxSource) {
        Reason("memory_write_invalid_source"); return FALSE;
    }
    if (*reinterpret_cast<int*>(editor + 0xC8)) {
        Reason("memory_write_active_selection"); return FALSE;
    }
    std::string before, after;
    if (!Serialize(editor, before)) return FALSE;
    std::string source(text);
    if (source.back() != '\n') source += "\r\n";
    if (!ParseAndCommit(editor, source.data(), source.size()) || !Serialize(editor, after)) return FALSE;
    if (before == after) { Reason("memory_commit_no_source_change"); return FALSE; }
    Reason("memory_model_write_success"); return TRUE;
}
extern "C" __declspec(dllexport) BOOL WINAPI JadeHookGenerateAssembly(
    const char* assembly, const char* channel, const char* handler) {
    Operation op; if (!op.entered) return FALSE;
    if (!assembly || !*assembly || !channel || !handler) {
        Reason("memory_generate_invalid_arguments"); return FALSE;
    }
    uintptr_t previous = *At<uintptr_t*>(0x675B44);
    // Project item creation is an IDE command, not source text transport.
    SendMessageA(MainWindow(), WM_COMMAND, 32782, 0);
    uintptr_t editor = Editor();
    if (!editor || editor == previous) { Reason("memory_new_editor_not_activated"); return FALSE; }
    Command(editor, 0x01010042, 2, 1, 0);
    if (!Command(editor, 0x02020071, 0x0F, reinterpret_cast<uintptr_t>(assembly), 0)) {
        Reason("memory_assembly_rename_failed"); return FALSE;
    }
    Reason("memory_assembly_created"); return TRUE;
}
// No background thread may enter the IDE model or outlive this module.
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
