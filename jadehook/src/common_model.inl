// Add a complete, independent common assembly plus an optional subscription
// stub in one IDE undo transaction. Existing routines are never overwritten.
int EnsureCommon(const char* name, const char* source, const char* subscriptionName,
    const char* fixedName) {
    for (const char* s : {name, source, subscriptionName, fixedName})
        if (!s || !*s || strnlen_s(s, kMaxSource + 1) > kMaxSource) {
            Reason("common_invalid_input"); return -1;
        }
    if (!_stricmp(name, subscriptionName)) { Reason("common_invalid_input"); return -1; }
    BYTE* project = Project();
    if (U32(project,852) == 1 || U32(project,848) || *At<int*>(0x67625C) != -1) {
        Reason("common_project_busy_or_readonly"); return -1;
    }
    ModelItem common, subscription, fixed, parsed;
    const int exists = FindModel(project + 640, name, common);
    const int subscriptions = FindModel(project + 640, subscriptionName, subscription);
    if (exists < 0 || exists > 1 || subscriptions < 0 || subscriptions > 1) {
        Reason("common_model_ambiguous"); return -1;
    }
    if ((common.value && ((common.id & 0x40000000) || U32(common.value,68))) ||
        (subscription.value && ((subscription.id & 0x40000000) || U32(subscription.value,68)))) {
        Reason("common_assembly_not_editable"); return -1;
    }
    if (std::string(source).find(AnsiLiteral(L", \u7c7b_json")) != std::string::npos &&
        FindModel(project + 640,AnsiLiteral(L"\u7c7b_json").c_str(),parsed) != 1) {
        Reason("common_json_dependency_missing"); return -1;
    }
    const int fixedCount = FindModel(project + 612, fixedName, fixed);
    const int ownedFixed = subscriptions ? FindSub(project,subscription.value,fixedName,parsed) : 0;
    if (fixedCount < 0 || fixedCount > 1 || fixedCount != ownedFixed) {
        Reason("common_subscription_name_conflict"); return -1;
    }
    Package additions, stub, apiAdditions, assemblyOnly;
    if (!ParseSource(additions,source,false) || ModelCount(additions.bytes + 640) != 1 ||
        FindModel(additions.bytes + 640,name,parsed) != 1 ||
        ModelCount(additions.bytes + 612) <= 0 || U32(additions.bytes,508)) {
        Reason("common_template_invalid"); return -1;
    }
    std::vector<std::string> names;
    // Compare the option marker before name checks: a different profile must not
    // be mistaken for a completed prior generation, or overwrite user changes.
    ModelItem requestedProfile, existingProfile;
    const auto profileName = AnsiLiteral(L"Jade_\u516c\u5171_\u751f\u6210\u914d\u7f6e");
    if (exists && FindSub(additions.bytes,parsed.value,profileName.c_str(),requestedProfile) == 1) {
        Buffer requestedText, existingText;
        std::string wanted, present;
        SerializeSub(requestedProfile, requestedText);
        if (FindSub(project,common.value,profileName.c_str(),existingProfile) != 1) {
            Reason("common_profile_conflict"); return -1;
        }
        SerializeSub(existingProfile, existingText);
        if (!BufferString(requestedText,wanted) || !BufferString(existingText,present) ||
            Comparable(wanted) != Comparable(present)) {
            Reason("common_profile_conflict"); return -1;
        }
    }
    std::string expectedAssembly(source);
    // Optional Windows restore/state helpers live in the global DLL table (668),
    // not the assembly. Preflight them before importing anything; never replace
    // an application's existing declarations. Reject other global additions.
    if (ModelCount(additions.bytes + 552) || ModelCount(additions.bytes + 696) ||
        U32(additions.bytes,728)) { Reason("common_template_invalid"); return -1; }
    std::vector<std::string> apiNames;
    const int apiCount = ModelCount(additions.bytes + 668);
    if (apiCount < 0) { Reason("common_template_invalid"); return -1; }
    if (apiCount) {
        // Only the known preamble contains DLL declarations. Do not serialize
        // an unresolved package to build the expected text: flow-node headers
        // there have not acquired their leading dots until IDE symbol binding.
        const auto firstApi = expectedAssembly.find(AnsiLiteral(L".DLL\u547d\u4ee4 "));
        const auto firstAssembly = expectedAssembly.find(AnsiLiteral(L".\u7a0b\u5e8f\u96c6 "));
        if (firstApi == std::string::npos || firstAssembly == std::string::npos || firstApi >= firstAssembly) {
            Reason("common_template_invalid"); return -1;
        }
        expectedAssembly.erase(firstApi,firstAssembly-firstApi);
        // Keep declarations separate so they can enter the user section before
        // imported EC definitions, rather than after the dependency boundary.
        const std::string apiSource = std::string(source).substr(0, firstAssembly);
        if (!ParseSource(apiAdditions,apiSource,false) ||
            ModelCount(apiAdditions.bytes + 668) != apiCount ||
            ModelCount(apiAdditions.bytes + 640) || ModelCount(apiAdditions.bytes + 612) ||
            !ParseSource(assemblyOnly,expectedAssembly,false)) {
            Reason("common_template_invalid"); return -1;
        }
    }
    for (int i=0; i<apiCount; ++i) {
        const auto item=ModelAt(additions.bytes + 668,i);
        if (!item.value || !Ptr(item.value,0)) { Reason("common_template_invalid"); return -1; }
        const std::string key(reinterpret_cast<const char*>(Ptr(item.value,0)));
        ModelItem found;
        if (FindModel(additions.bytes + 668,key.c_str(),found) != 1 ||
            FindModel(project + 668,key.c_str(),found) != (exists ? 1 : 0) ||
            FindModel(project + 612,key.c_str(),found) != 0) {
            Reason("common_api_name_conflict"); return -1;
        }
        apiNames.push_back(key);
    }
    for (int i = 0; i < ModelCount(additions.bytes + 612); ++i) {
        const auto item = ModelAt(additions.bytes + 612,i);
        if (!item.value || !Ptr(item.value,0)) { Reason("common_template_invalid"); return -1; }
        const std::string subName(reinterpret_cast<const char*>(Ptr(item.value,0)));
        ModelItem global, owned;
        const int globalCount = FindModel(project + 612,subName.c_str(),global);
        if (FindModel(additions.bytes + 612,subName.c_str(),parsed) != 1 ||
            globalCount != (exists ? 1 : 0) ||
            (exists && FindSub(project,common.value,subName.c_str(),owned) != 1)) {
            Reason("common_routine_name_conflict"); return -1;
        }
        names.push_back(subName);
    }
    if (exists) {
        Reason(fixedCount ? "common_already_exists_preserved" : "common_existing_incomplete");
        return fixedCount ? 2 : -1;
    }
    // Do not silently replace an application's existing window IPC bindings.
    const auto requested = Comparable(source);
    const int projectSubCount = ModelCount(project + 612);
    if (projectSubCount < 0) { Reason("common_model_ambiguous"); return -1; }
    for (int i = 0; i < projectSubCount; ++i) {
        Buffer text; SerializeSub(ModelAt(project + 612,i),text);
        std::string existing;
        if (!BufferString(text,existing)) { Reason("common_preflight_read_failed"); return -1; }
        const auto normalized = Comparable(existing);
        for (const auto* channel : {L"\"win:minimize\"", L"\"win:maximize\"", L"\"win:close\""})
            if (requested.find(channel) != std::wstring::npos && normalized.find(channel) != std::wstring::npos) {
                Reason("common_window_channel_conflict"); return -1;
            }
    }
    std::string original;
    if (subscriptions && !SerializeAssembly(project,subscription,original)) {
        Reason("common_preflight_read_failed"); return -1;
    }
    if (!fixedCount) {
        std::string stubSource = AnsiLiteral(L".\u5b50\u7a0b\u5e8f ") + fixedName + "\r\n";
        if (!subscriptions) stubSource = AnsiLiteral(L".\u7248\u672c 2\r\n.\u7a0b\u5e8f\u96c6 ") +
            subscriptionName + "\r\n\r\n" + stubSource;
        if (!ParseSource(stub,stubSource,subscriptions != 0) ||
            ModelCount(stub.bytes + 640) != (subscriptions ? 0 : 1) ||
            ModelCount(stub.bytes + 612) != 1 || FindModel(stub.bytes + 612,fixedName,parsed) != 1) {
            Reason("common_stub_parse_failed"); return -1;
        }
    }
    const uintptr_t active = *At<uintptr_t*>(0x675B44);
    BackgroundUndo undo;
    const auto import = [&](Package& package) {
        At<void(__thiscall*)(void*,void*,void*,void*,void*,int)>(0x4AA570)(
            At<void*>(0x6756E0),project,package.bytes,nullptr,nullptr,0);
    };
    bool ok = true;
    if (!fixedCount) {
        if (subscriptions) ok = AddSubroutines(stub,project,subscription);
        else import(stub);
    }
    if (ok && apiCount) {
        using Resolve = void(__thiscall*)(void*,void*,int,int,void*,void*,void*,void*,int);
        At<Resolve>(0x464880)(apiAdditions.bytes,project,0,-1,nullptr,nullptr,nullptr,nullptr,0);
        int index = ModelCount(project + 668);
        for (int i=0; i<index; ++i) {
            if (U32(ModelAt(project + 668,i).value,8) & 4) { index=i; break; }
        }
        // Same insertion/undo pair as the DLL editor (497543..49757E).
        At<void(__thiscall*)(void*,int,int,void*,int,int)>(0x50F120)(
            At<void*>(0x6756E0),302,index,apiAdditions.bytes + 668,0,apiCount);
        At<void(__thiscall*)(void*,int,void*,int,int)>(0x4F4A50)(
            project + 668,index,apiAdditions.bytes + 668,0,apiCount);
        At<void(__thiscall*)(void*)>(0x491AB0)(apiAdditions.bytes + 676);
        At<void(__thiscall*)(void*,void*)>(0x4F14F0)(project + 756,apiAdditions.bytes + 756);
    }
    if (ok) import(apiCount ? assemblyOnly : additions);
    ok = ok && FindModel(project + 640,name,common) == 1 &&
        FindModel(project + 640,subscriptionName,subscription) == 1 &&
        FindSub(project,subscription.value,fixedName,fixed) == 1;
    std::string actual;
    ok = ok && SerializeAssembly(project,common,actual) && Comparable(actual) == Comparable(expectedAssembly);
    for (const auto& apiName : apiNames)
        ok = ok && FindModel(project + 668,apiName.c_str(),parsed) == 1;
    for (const auto& subName : names)
        ok = ok && FindSub(project,common.value,subName.c_str(),parsed) == 1;
    if (ok && fixedCount) {
        std::string after;
        ok = SerializeAssembly(project,subscription,after) && after == original;
    }
    ok = ok && *At<uintptr_t*>(0x675B44) == active;
    undo.Close();
    if (!ok) {
        SendMessageA(MainWindow(),WM_COMMAND,0xE12B,0);
        bool commonGone = FindModel(project + 640,name,parsed) == 0;
        for (const auto& apiName : apiNames)
            commonGone = commonGone && FindModel(project + 668,apiName.c_str(),parsed) == 0;
        const int remaining = FindModel(project + 640,subscriptionName,parsed);
        std::string after;
        const bool restored = subscriptions ? remaining == 1 &&
            SerializeAssembly(project,parsed,after) && after == original : remaining == 0;
        Reason(commonGone && restored ? "common_verification_failed_rolled_back" : "common_rollback_failed_stop");
        return -1;
    }
    At<void(__thiscall*)(void*)>(0x469AD0)(At<void*>(0x6756E0));
    At<void(__thiscall*)(void*,void*)>(0x468D40)(At<void*>(0x6756E0),nullptr);
    Reason("common_created_verified"); return 1;
}
