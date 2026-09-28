#include "../src/ProjectWebDetection.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
int passed = 0;
void Check(bool value, const char* name)
{
    if (!value) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
    ++passed;
    std::printf("PASS %s\n", name);
}

struct Fixture {
    std::filesystem::path root, web;
    Fixture() {
        wchar_t temp[MAX_PATH]{}, unique[MAX_PATH]{};
        Check(GetTempPathW(MAX_PATH, temp) != 0 && GetTempFileNameW(temp, L"jwd", 0, unique) != 0,
            "allocate isolated fixture directory");
        DeleteFileW(unique);
        root = unique;
        web = root / L"web";
        std::filesystem::create_directories(web / L"js");
    }
    ~Fixture() { std::error_code error; std::filesystem::remove_all(root, error); }
    void Write(const std::filesystem::path& relative, const std::string& bytes) {
        std::ofstream output(root / relative, std::ios::binary);
        output << bytes;
        Check(output.good(), "write isolated fixture");
    }
};
}

int wmain(int argc, wchar_t** argv)
{
    using ProjectWebDetection::Marker;
    Check(Marker("<button data-jade-handler=\"导入账号_被单击\">导入账号</button>"
        "<script src=\"app.js?v=20260904-12\"></script>") == "data-jade-handler",
        "handler-only HTML with an external script enables Jade preview");
    for (const std::string marker : {"data-jade-control", "data-jade-channel", "jade.invoke", "window.jade"})
        Check(Marker(marker) == marker, "existing Jade markers still enable preview");
    Check(Marker("").empty(), "empty HTML does not enable takeover");
    Check(Marker("<button id=\"save\">Save</button><script src=\"app.js\"></script>").empty(),
        "ordinary HTML without Jade markers does not enable takeover");
    using ProjectWebDetection::ScriptReferences;
    Check(ScriptReferences("<SCRIPT defer SRC = 'app.js?v=1#x'></SCRIPT>") ==
        std::vector<std::wstring>{L"app.js?v=1#x"}, "case-insensitive tags and quoted attributes");
    Check(ScriptReferences("<script type=module src=js/app.mjs></script>") ==
        std::vector<std::wstring>{L"js/app.mjs"}, "module and unquoted source");
    Check(ScriptReferences("<!-- <script src='app.js'></script> -->").empty(), "ignore commented references");
    Check(ScriptReferences("<template><script src='app.js'></script></template>").empty(), "ignore template scripts");
    Check(ScriptReferences("<textarea><script src='app.js'></script></textarea>").empty(), "ignore textarea content");
    Check(ScriptReferences("<script>const s=\"<script src='app.js'>\";</script>").empty(), "ignore script text references");
    Check(ScriptReferences("<div title=\"<script src='app.js'>\"></div>").empty(), "ignore attribute text references");
    Check(ScriptReferences("<script type=application/json src=app.js></script>").empty(), "ignore data scripts");
    Check(ScriptReferences("<script src=app.js></script><base href='https://example.invalid/'>").empty(),
        "do not guess script resolution with a base URL");

    Fixture fixture;
    fixture.Write(L"web/app.js", "function minimizeWindow(){jade.invoke('win:minimize');}");
    fixture.Write(L"web/plain.js", "document.body.classList.add('ready');");
    fixture.Write(L"web/js/listener.mjs", "jade.on('message',()=>{});");
    fixture.Write(L"web/脚本.js", "window.jade.invoke('ready');");
    fixture.Write(L"outside.js", "jade.invoke('outside');");
    std::string reason;
    const auto detect = [&](std::string_view html) {
        return ProjectWebDetection::Detect(html, fixture.web.wstring(), reason);
    };
    Check(detect("<script src='app.js'></script>"), "external Jade script enables preview");
    Check(reason.find("app.js marker=jade.invoke") != reason.npos, "reason identifies the external script");
    Check(detect("<SCRIPT SRC='./app.js?v=1&amp;x=2#ready'></SCRIPT>"), "strip version query and fragment");
    Check(detect("<script type=module src=js/listener.mjs></script>"), "event-only external script enables preview");
    Check(detect("<script src='%E8%84%9A%E6%9C%AC.js'></script>"), "UTF-8 escaped local script path");
    Check(detect("<script src='js/../app.js'></script>"), "normalized relative path inside web");
    Check(!detect("<script src='plain.js'></script>"), "ordinary page stays native despite unrelated Jade files");
    Check(!detect("<script src='missing.js'></script>"), "missing script does not enable takeover");
    Check(!detect("<!-- <script src='app.js'></script> -->"), "comment-only page stays native");
    Check(!detect("<script src='../outside.js'></script>"), "reject outside-web script");
    for (const auto* reference : {L"../outside.js", L"%2e%2e/outside.js", L"/app.js", L"//server/app.js",
        L"https://example.invalid/app.js", L"file:///C:/app.js", L"C:\\app.js", L"app.js%00", L"app.js:stream"})
        Check(ProjectWebDetection::LocalScriptPath(fixture.web.wstring(), reference).empty(),
            "reject remote, absolute, traversal, device-stream or invalid paths");
    fixture.Write(L"web/large.js", std::string(512 * 1024, ' ') + "jade.invoke('beyond-limit');");
    Check(!detect("<script src='large.js'></script>"), "script reads stay bounded");
    std::string many;
    for (int i = 0; i < 33; ++i) many += "<script src='missing.js'></script>";
    Check(ScriptReferences(many).size() == 32, "reference count stays bounded");
    if (argc > 1) {
        std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
        Check(input.is_open(), "project HTML can be read");
        const std::string html{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        Check(ProjectWebDetection::Detect(html, std::filesystem::path(argv[1]).parent_path().wstring(), reason),
            "actual project enables preview via HTML or local scripts");
        std::printf("Actual project detection: %s\n", reason.c_str());
    }
    std::printf("%d project web detection checks passed\n", passed);
}
