// Exact-build project-model operations. No editor activation or caret commands.
struct ModelItem { DWORD id = 0; BYTE* value = nullptr; };
DWORD U32(const void* p, size_t offset) {
    return *reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(p) + offset);
}
BYTE* Ptr(const void* p, size_t offset) {
    return *reinterpret_cast<BYTE* const*>(static_cast<const BYTE*>(p) + offset);
}
BYTE* Project() { return At<BYTE*>(0x6757A0); }
int ModelCount(const void* table) {
    const DWORD bytes = U32(table, 24);
    return bytes % 8 || bytes > 8000000 || (bytes && !Ptr(table, 16)) ? -1 : bytes / 8;
}
ModelItem ModelAt(BYTE* table, int index) {
    const int count = ModelCount(table);
    if (index < 0 || index >= count) return {};
    BYTE* data = Ptr(table, 16);
    return {U32(data, index * 4), Ptr(data, (count + index) * 4)};
}
int FindModel(BYTE* table, const char* name, ModelItem& found) {
    const int count = ModelCount(table);
    if (count < 0) return -1;
    int copies = 0;
    for (int i = 0; i < count; ++i) {
        const auto item = ModelAt(table, i);
        if (!item.value || !Ptr(item.value, 0)) return -1;
        if (!_stricmp(reinterpret_cast<const char*>(Ptr(item.value, 0)), name)) {
            found = item; ++copies;
        }
    }
    return copies;
}
int FindSub(BYTE* project, BYTE* assembly, const char* name, ModelItem& found) {
    const DWORD bytes = U32(assembly, 60);
    if (bytes % 4 || bytes > 4000000 || (bytes && !Ptr(assembly, 52))) return -1;
    int copies = 0;
    for (DWORD offset = 0; offset < bytes; offset += 4) {
        const DWORD id = U32(Ptr(assembly, 52), offset);
        BYTE* sub = nullptr;
        if (!At<int(__thiscall*)(void*, DWORD, void*, void*)>(0x4F4970)(
            project + 612, id, &sub, nullptr) || !sub || !Ptr(sub, 0)) return -1;
        if (!_stricmp(reinterpret_cast<const char*>(Ptr(sub, 0)), name)) {
            found = {id, sub}; ++copies;
        }
    }
    return copies;
}
void Newline(Buffer& text) {
    At<void(__thiscall*)(void*, const char*)>(0x491C60)(text.words, "\r\n");
}
void SerializeSub(ModelItem item, Buffer& text) {
    Newline(text);
    At<void(__thiscall*)(void*, void*)>(0x4D5170)(item.value, text.words);
    using Variable = void(__cdecl*)(void*, int, void*, int, int);
    for (DWORD i = 0; i < U32(item.value, 60); ++i)
        At<Variable>(0x4D4AA0)(item.value + 56, i, text.words, 4, 0);
    for (DWORD i = 0; i < U32(item.value, 28); ++i)
        At<Variable>(0x4D4AA0)(item.value + 24, i, text.words, 3, 0);
    At<void(__stdcall*)(void*, int, int, void*, int)>(0x4D59E0)(
        item.value, 0, static_cast<int>(U32(item.value, 104) / 4) - 1, text.words, 0);
}
bool BufferString(const Buffer& text, std::string& out) {
    if (!text.data() || !text.size() || text.size() > kMaxSource) return false;
    out.assign(text.data(), text.size());
    while (!out.empty() && !out.back()) out.pop_back();
    return !out.empty();
}
bool SerializeAssembly(BYTE* project, ModelItem assembly, std::string& out) {
    Buffer text;
    At<void(__cdecl*)(void*)>(0x4D48A0)(text.words);
    At<void(__cdecl*)(void*, void*)>(0x4D50B0)(assembly.value, text.words);
    for (DWORD i = 0; i < U32(assembly.value, 16); ++i)
        At<void(__cdecl*)(void*, int, void*, int, int)>(0x4D4AA0)(
            assembly.value + 12, i, text.words, 2, 0);
    const DWORD bytes = U32(assembly.value, 60);
    if (bytes % 4 || bytes > 4000000) return false;
    for (DWORD i = 0; i < bytes; i += 4) {
        const DWORD id = U32(Ptr(assembly.value, 52), i);
        BYTE* sub = nullptr;
        if (!At<int(__thiscall*)(void*, DWORD, void*, void*)>(0x4F4970)(
            project + 612, id, &sub, nullptr) || !sub) return false;
        SerializeSub({id, sub}, text);
        if (text.size() > kMaxSource) return false;
    }
    return BufferString(text, out);
}
std::string AnsiLiteral(const wchar_t* source) {
    int size = WideCharToMultiByte(936, 0, source, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(936, 0, source, -1, out.data(), size, nullptr, nullptr);
    out.pop_back(); return out;
}
std::wstring Comparable(const std::string& ansi) {
    int n = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, ansi.data(),
        static_cast<int>(ansi.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring wide(n, L'\0'), result;
    MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, ansi.data(), static_cast<int>(ansi.size()), wide.data(), n);
    bool quoted = false;
    for (wchar_t c : wide) {
        if (c == L'\u201c' || c == L'\u201d') c = L'"';
        if (c == L'"') quoted = !quoted;
        if (!quoted && (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n')) continue;
        if (!quoted && c == L'\uff08') c = L'(';
        if (!quoted && c == L'\uff09') c = L')';
        if (!quoted && c == L'\uff0c') c = L',';
        result.push_back(c);
    }
    return result;
}
bool HasStatement(ModelItem sub, const char* statement) {
    Buffer text; SerializeSub(sub, text);
    std::string source;
    if (!BufferString(text, source)) return false;
    const auto needle = Comparable(statement);
    if (needle.empty()) return false;
    size_t start = 0;
    while (start < source.size()) {
        size_t end = source.find('\n', start);
        if (end == std::string::npos) end = source.size();
        if (Comparable(source.substr(start, end - start)) == needle) return true;
        start = end + 1;
    }
    return false;
}
bool ParseSource(Package& package, const std::string& source, bool partial) {
    if (source.empty() || source.size() > kMaxSource) return false;
    Buffer text;
    At<void(__thiscall*)(void*, const void*, int)>(0x492170)(text.words, source.data(), static_cast<int>(source.size()));
    At<void(__thiscall*)(void*, int)>(0x491BB0)(text.words, 0);
    if (!At<int(__thiscall*)(void*, void*)>(0x4D59B0)(package.bytes, text.words)) return false;
    // Partial paste packages carry an anonymous scratch subroutine. Match the
    // IDE's ownership handoff before symbol resolution, just as ParseAndCommit.
    if (partial) for (int i = static_cast<int>(U32(package.bytes, 508)) - 1; i >= 0; --i) {
        BYTE* item = Ptr(Ptr(package.bytes, 504), i * 4);
        for (int offset : {56, 24})
            At<void(__thiscall*)(void*, void*, int, int)>(0x4E7380)(
                package.bytes + 580, item + offset, 0, U32(item, offset + 4));
        if (U32(item, 204)) {
            for (int offset : {56, 24}) {
                auto clear = *reinterpret_cast<void(__thiscall**)(void*)>(Ptr(item, offset) + 8);
                clear(item + offset);
            }
        } else {
            At<void(__thiscall*)(void*)>(0x40F4E0)(item);
            At<void(__cdecl*)(void*)>(0x5BD402)(item);
            At<void(__thiscall*)(void*, int, int)>(0x5BB445)(package.bytes + 500, i, 1);
        }
    }
    return true;
}
void ResolvePackage(Package& package, BYTE* project, ModelItem assembly, ModelItem sub = {}) {
    using Resolve = void(__thiscall*)(void*, void*, int, int, void*, void*, void*, void*, int);
    At<Resolve>(0x464880)(package.bytes, project, sub.value ? 13 : 8,
        sub.value ? 0x20000000 : 0x10000000, assembly.value, assembly.value + 12,
        sub.value ? sub.value + 56 : nullptr, sub.value ? sub.value + 24 : nullptr, 0);
}
void MergeAuxiliary(Package& package, BYTE* project) {
    for (const auto pair : {std::pair<int,int>{552,301}, {668,302}, {696,303}}) {
        const int count = ModelCount(package.bytes + pair.first);
        if (!count) continue;
        At<void(__stdcall*)(int,int,void*,int,int)>(0x50F120)(
            pair.second, ModelCount(project + pair.first), package.bytes + pair.first, 0, count);
        At<void(__thiscall*)(void*,void*)>(0x4F4B10)(project + pair.first, package.bytes + pair.first);
        At<void(__thiscall*)(void*)>(0x491AB0)(package.bytes + pair.first + 8);
    }
    At<void(__stdcall*)(int,int,void*,int,int)>(0x50F370)(
        307, U32(project, 728), package.bytes + 724, 0, U32(package.bytes, 728));
    At<void(__thiscall*)(void*,void*)>(0x4E7490)(project + 724, package.bytes + 724);
    At<void(__thiscall*)(void*,void*)>(0x4F14F0)(project + 756, package.bytes + 756);
}
bool AddSubroutines(Package& package, BYTE* project, ModelItem assembly) {
    ResolvePackage(package, project, assembly);
    // These generated packages must contain model subroutines, not loose code.
    if (U32(package.bytes, 508) || ModelCount(package.bytes + 640) != 0) return false;
    const int count = ModelCount(package.bytes + 612);
    if (count <= 0) return false;
    MergeAuxiliary(package, project);
    const int index = U32(assembly.value, 60) / 4;
    for (int i = 0; i < count; ++i)
        *reinterpret_cast<DWORD*>(ModelAt(package.bytes + 612, i).value + 12) = assembly.id;
    At<void(__stdcall*)(DWORD,int,int,void*,int,int)>(0x50DFA0)(
        assembly.id, index, ModelCount(project + 612), package.bytes + 612, 0, count);
    At<void(__thiscall*)(void*,int,const void*,int)>(0x4920D0)(
        assembly.value + 44, index * 4, Ptr(package.bytes + 612, 16), count * 4);
    At<void(__thiscall*)(void*,void*)>(0x4F4B10)(project + 612, package.bytes + 612);
    At<void(__thiscall*)(void*)>(0x491AB0)(package.bytes + 620);
    return true;
}
bool AppendBody(Package& package, BYTE* project, ModelItem assembly, ModelItem sub) {
    ResolvePackage(package, project, assembly, sub);
    if (ModelCount(package.bytes + 612) || ModelCount(package.bytes + 640)) return false;
    if (!U32(package.bytes, 508) || !U32(sub.value, 104)) return false;
    MergeAuxiliary(package, project);
    Buffer ranges;
    At<int(__cdecl*)(DWORD,void*,int,int,void*,void*,int)>(0x4A0B80)(
        sub.id, sub.value, U32(sub.value, 104) / 4 - 1, 1, package.bytes, ranges.words, 0);
    return true;
}
struct BackgroundUndo {
    Buffer state;
    uintptr_t editor = *At<uintptr_t*>(0x675B44);
    bool closed = false;
    void Capture() {
        if (editor) {
            using Fn = void(__thiscall*)(uintptr_t,void*);
            (*reinterpret_cast<Fn*>(*reinterpret_cast<uintptr_t*>(editor) + 0xD0))(editor, state.words);
        }
    }
    BackgroundUndo() { Capture(); At<void(__thiscall*)(void*,void*)>(0x50BE60)(At<void*>(0x676258),state.words); }
    void Close() {
        if (closed) return;
        Capture(); At<void(__thiscall*)(void*,void*)>(0x50BF30)(At<void*>(0x676258),state.words); closed = true;
    }
    ~BackgroundUndo() { Close(); }
};
// 1 = changed in background; 2 = already complete; -1 = refused/failed.
int EnsureBackground(const char* callbackAssemblyName, const char* assemblyName, const char* fixedName, const char* handlerName,
    const char* statement, const char* callback) {
    for (const char* s : {callbackAssemblyName, assemblyName, fixedName, handlerName, statement, callback})
        if (!s || !*s || strnlen_s(s, kMaxSource + 1) > kMaxSource) {
            Reason("background_invalid_input"); return -1;
        }
    BYTE* project = Project();
    if (U32(project,852) == 1 || U32(project,848) || *At<int*>(0x67625C) != -1) {
        Reason("background_project_busy_or_readonly"); return -1;
    }
    const bool sameAssembly = !_stricmp(callbackAssemblyName,assemblyName);
    ModelItem assembly, callbackAssembly, fixed, handler;
    const int assemblies = FindModel(project + 640, assemblyName, assembly);
    const int callbackAssemblies = sameAssembly ? assemblies : FindModel(project + 640,callbackAssemblyName,callbackAssembly);
    if (sameAssembly) callbackAssembly = assembly;
    const int fixedCount = assemblies == 1 ? FindSub(project,assembly.value,fixedName,fixed) : 0;
    const int handlerCount = callbackAssemblies == 1 ? FindSub(project,callbackAssembly.value,handlerName,handler) : 0;
    if (assemblies < 0 || assemblies > 1 || callbackAssemblies < 0 || callbackAssemblies > 1 ||
        fixedCount < 0 || fixedCount > 1 || handlerCount < 0 || handlerCount > 1) {
        Reason("background_model_ambiguous"); return -1;
    }
    if ((assembly.value && ((assembly.id & 0x40000000) || U32(assembly.value,68))) ||
        (callbackAssembly.value && ((callbackAssembly.id & 0x40000000) || U32(callbackAssembly.value,68)))) {
        Reason("background_assembly_not_editable"); return -1;
    }
    const bool statementExists = fixedCount == 1 && HasStatement(fixed, statement);
    if (handlerCount == 1 && statementExists) { Reason("background_already_complete"); return 2; }
    const auto header = AnsiLiteral(L".\u5b50\u7a0b\u5e8f ");
    std::string pending;
    if (!fixedCount) pending += header + fixedName + "\r\n" + statement + "\r\n";
    if (sameAssembly && !handlerCount) pending += std::string(callback) + "\r\n";
    Package additions, line, callbacks;
    const auto assemblyHeader = AnsiLiteral(L".\u7248\u672c 2\r\n\r\n.\u7a0b\u5e8f\u96c6 ");
    if (!assemblies) pending = assemblyHeader + assemblyName + "\r\n\r\n" + pending;
    std::string callbackPending;
    if (!sameAssembly && !handlerCount) {
        callbackPending = std::string(callback) + "\r\n";
        if (!callbackAssemblies) callbackPending = assemblyHeader + callbackAssemblyName + "\r\n\r\n" + callbackPending;
    }
    if ((!pending.empty() && !ParseSource(additions, pending, assemblies != 0)) ||
        (!callbackPending.empty() && !ParseSource(callbacks,callbackPending,callbackAssemblies != 0)) ||
        (fixedCount && !statementExists && !ParseSource(line, statement, true))) {
        Reason("background_source_parse_failed"); return -1;
    }
    if (!pending.empty()) {
        ModelItem parsed;
        if (ModelCount(additions.bytes + 640) != (assemblies ? 0 : 1) ||
            ModelCount(additions.bytes + 612) != (!fixedCount + (sameAssembly && !handlerCount)) ||
            (!fixedCount && FindModel(additions.bytes + 612, fixedName, parsed) != 1) ||
            (sameAssembly && !handlerCount && FindModel(additions.bytes + 612, handlerName, parsed) != 1)) {
            Reason("background_package_shape_invalid"); return -1;
        }
    }
    if (!callbackPending.empty()) {
        ModelItem parsed;
        if (ModelCount(callbacks.bytes + 640) != (callbackAssemblies ? 0 : 1) ||
            ModelCount(callbacks.bytes + 612) != 1 || FindModel(callbacks.bytes + 612,handlerName,parsed) != 1) {
            Reason("background_callback_package_shape_invalid"); return -1;
        }
    }
    std::string original, originalCallback;
    if (assemblies && !SerializeAssembly(project,assembly,original)) {
        Reason("background_preflight_read_failed"); return -1;
    }
    if (!sameAssembly && callbackAssemblies && !SerializeAssembly(project,callbackAssembly,originalCallback)) {
        Reason("background_callback_preflight_read_failed"); return -1;
    }
    const uintptr_t active = *At<uintptr_t*>(0x675B44);
    BackgroundUndo undo;
    bool ok = true;
    if (!callbackPending.empty()) {
        if (!callbackAssemblies) {
            At<void(__thiscall*)(void*,void*,void*,void*,void*,int)>(0x4AA570)(
                At<void*>(0x6756E0), project, callbacks.bytes, nullptr, nullptr, 0);
            ok = FindModel(project + 640,callbackAssemblyName,callbackAssembly) == 1;
        } else ok = AddSubroutines(callbacks,project,callbackAssembly);
    }
    if (ok && !assemblies) {
        At<void(__thiscall*)(void*,void*,void*,void*,void*,int)>(0x4AA570)(
            At<void*>(0x6756E0), project, additions.bytes, nullptr, nullptr, 0);
        ok = FindModel(project + 640, assemblyName, assembly) == 1;
    } else if (ok && !pending.empty()) {
        ok = AddSubroutines(additions, project, assembly);
    }
    if (sameAssembly) callbackAssembly = assembly;
    if (ok && fixedCount && !statementExists) ok = AppendBody(line,project,assembly,fixed);
    ok = ok && FindSub(project,assembly.value,fixedName,fixed) == 1 &&
        FindSub(project,callbackAssembly.value,handlerName,handler) == 1 && HasStatement(fixed,statement);
    if (ok && !handlerCount) {
        Buffer text; SerializeSub(handler,text);
        std::string actual;
        ok = BufferString(text,actual) && Comparable(actual) == Comparable(callback);
    }
    undo.Close();
    if (!ok) {
        SendMessageA(MainWindow(), WM_COMMAND, 0xE12B, 0);
        ModelItem restored;
        const int remaining = FindModel(project + 640,assemblyName,restored);
        std::string actual;
        bool rolledBack = assemblies ? remaining == 1 && SerializeAssembly(project,restored,actual) && actual == original : remaining == 0;
        if (!sameAssembly) {
            const int callbacksRemaining = FindModel(project + 640,callbackAssemblyName,restored);
            rolledBack = rolledBack && (callbackAssemblies ? callbacksRemaining == 1 && SerializeAssembly(project,restored,actual) && actual == originalCallback : callbacksRemaining == 0);
        }
        Reason(rolledBack ? "background_verification_failed_rolled_back" : "background_rollback_failed_stop"); return -1;
    }
    At<void(__thiscall*)(void*)>(0x469AD0)(At<void*>(0x6756E0));
    At<void(__thiscall*)(void*,void*)>(0x468D40)(At<void*>(0x6756E0),nullptr);
    if (*At<uintptr_t*>(0x675B44) != active) {
        Reason("background_unexpected_editor_change"); return -1;
    }
    Reason(handlerCount == 0 ? "background_callback_created_verified" :
        "background_subscription_repaired_verified"); return 1;
}
