#include "../src/DesignerInspection.h"
#include "../src/CommonCode.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
int passed = 0;
void Check(bool value, const char* name) {
    if (!value) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
    ++passed;
    std::printf("PASS %s\n", name);
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace DesignerInspection;
    const std::string oldExit = ".子程序 退出, 逻辑型, 公开\n返回 (匿名API_65 ())\n";
    // Signature read from the user's currently imported JadeView module.
    const std::string newExit =
        ".子程序 退出, 逻辑型, 公开\r\n"
        ".参数 timeout_ms, 整数型, 可空, 等待超时时间，单位为毫秒。\r\n"
        ".如果 (是否为空 (timeout_ms))\r\n返回 (匿名API_65 ())\r\n.如果结束\r\n";
    Check(CallProblem(oldExit, "退出", 0).empty(), "old zero-argument exit remains callable");
    Check(CallProblem(newExit, "退出", 0).empty(), "optional timeout allows exit with zero arguments");
    Check(CallProblem(newExit, "退出", 1).empty(), "explicit optional timeout remains callable");
    Check(!CallProblem(newExit, "退出", 2).empty(), "extra arguments are rejected");
    Check(!CallProblem(newExit, "退出", -1).empty(), "negative arity is rejected");
    Check(!Method(newExit, "退出", 0) && Method(newExit, "退出", 1),
        "exact declaration matching remains separate from call compatibility");

    const std::string required = ".子程序 退出, 逻辑型, 公开\n.参数 timeout_ms, 整数型\n";
    Check(CallProblem(required, "退出", 0).find("第 1 个参数不可省略") != std::string::npos,
        "required timeout reports the unsupplied parameter");
    Check(CallProblem(required, "退出", 1).empty(), "required argument can be supplied");
    Check(!CallProblem("", "退出", 0).empty(), "missing method still blocks generation");
    Check(!CallProblem(newExit + newExit, "退出", 0).empty(), "duplicate method still blocks generation");
    Check(!OptionalParameter(".参数 可空, 整数型"), "parameter name is not an optional flag");
    Check(!OptionalParameter(".参数 x, 整数型, , 可空, 可空"), "description is not an optional flag");
    Check(!OptionalParameter(".参数 x, 整数型, 不可空"), "flag must match a complete token");
    Check(OptionalParameter(".参数 x, 整数型, 参考 可空 数组, 说明"), "combined flags support optional");
    Check(OptionalParameter(".参数 x, 整数型, \t可空 \t"), "flag whitespace is accepted");

    const std::string mixed =
        ".子程序 初始化, 逻辑型, 公开\n"
        ".参数 调试, 逻辑型, 可空\n"
        ".参数 标识, 文本型\n"
        ".参数 单实例, 逻辑型, 可空\n";
    Check(CallProblem(mixed, "初始化", 1).find("第 2 个参数不可省略") != std::string::npos,
        "optional arguments before a required argument do not lower its position");
    Check(CallProblem(mixed, "初始化", 2).empty(), "only optional trailing arguments may be omitted");
    Check(CallProblem(mixed, "初始化", 3).empty(), "all declared argument positions may be supplied");
    Check(Routines(mixed)[0].parameters == 3 && Routines(mixed)[0].minimumArguments == 2,
        "parser preserves total and minimum arity separately");
    const auto multi = newExit + ".子程序 消息循环\n' .参数 x, 整数型\n";
    Check(CallProblem(multi, "消息循环", 0).empty(), "parameters reset for each method and comments are ignored");
    Check(CallProblem("  .子程序 Exit\n\t.参数 timeout, 整数型, 可空\n", "exit", 0).empty(),
        "lookup handles indentation and ASCII case consistently");

    for (bool tray : {false, true}) for (bool single : {false, true}) {
        const auto source = CommonCode::BuildSource({tray, single, L"jade-inspection-test"});
        Check(!source.empty() && source.find(L"JadeView.App.退出 ()") != source.npos,
            "public template keeps non-waiting exit call");
        Check(source.find(L".DLL命令") == source.npos &&
            source.find(L"Interlocked") == source.npos && source.find(L"kernel32.dll") == source.npos,
            "public template does not introduce DLL commands");
    }
    if (argc > 1) {
        std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary);
        Check(input.is_open(), "current module snapshot is readable");
        const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        Check(CallProblem(source, "退出", 0).empty(), "current imported module accepts generated exit call");
        Check(CallProblem(source, "初始化", 6).empty(), "current imported module accepts initialization call");
        Check(CallProblem(source, "注册事件", 2).empty(), "current imported module accepts registration call");
        Check(CallProblem(source, "消息循环", 0).empty(), "current imported module accepts message loop call");
    }
    std::printf("%d designer inspection tests passed\n", passed);
    return 0;
}
