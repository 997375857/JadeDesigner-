#include "../src/CommonCode.h"
#include <string>
#include <cstdio>
#include <Windows.h>
#include <fstream>

int main(int argc, char**) {
    const std::wstring s(CommonCode::Source);
    int passed = 0;
    int total = 0;
    auto check = [&](bool ok, const char* name) {
        ++total;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        if (ok) ++passed;
    };
    check(s.find(L"_启动子程序") == std::wstring::npos, "No replacement startup");
    check(s.find(L"功能_获取ck") == std::wstring::npos && s.find(L"线程_") == std::wstring::npos, "No business tasks");
    check(s.find(L"App.注册事件") < s.find(L"App.初始化"), "Register ready before init");
    check(s.find(L"Jade_通讯_订阅 ()") < s.find(L"Jade_公共_创建主窗口 ()"), "Subscribe before opening page");
    check(s.find(L".窗口.最小化 (窗口ID)") != std::wstring::npos && s.find(L"设置窗口显示或隐藏") == std::wstring::npos, "Real minimize not tray hide");
    size_t count=0, p=0;
    while ((p=s.find(L".子程序 ",p)) != std::wstring::npos) { ++count; ++p; }
    check(count==9, "Nine common routines");
    check(s.find(L".参数 数据, 文本型") != std::wstring::npos, "IPC data parameter");
    check(s.find(L".窗口.销毁 (窗口ID)") != std::wstring::npos, "Close uses requesting window");
    using namespace CommonCode;
    check(ValidAppId(L"jade-demo_01") && !ValidAppId(L"short") && !ValidAppId(L"jade\"injection"), "App ID validation");
    check(ProjectAppId(L"C:/Demo/Test.e") == ProjectAppId(L"c:\\demo\\test.e") &&
        ProjectAppId(L"c:\\other\\test.e") != ProjectAppId(L"c:\\demo\\test.e"), "Stable distinct project IDs");
    Options parsed;
    check(ParseCommand(L"JADE_COMMAND\tgenerate_common\t1\t0\tjade-test",parsed) && parsed.tray && !parsed.singleInstance, "Options wire roundtrip");
    check(!ParseCommand(L"JADE_COMMAND\tgenerate_common\t1\t0\tjade-test\nINJECT",parsed) &&
        !ParseCommand(L"JADE_COMMAND\tgenerate_common\t2\t0\t",parsed), "Malformed options refused");
    check(ParseCommand(L"JADE_COMMAND\tgenerate_common",parsed) && !parsed.tray && !parsed.singleInstance, "Legacy command defaults");
    check(BuildSource({true,true,L"bad"}).empty(), "Invalid source configuration refused");
    for (int t=0;t<2;++t) for(int single=0;single<2;++single) {
        const auto code=BuildSource({t!=0,single!=0,L"jade-profile-test"});
        check(!code.empty() && code.find(L"生成配置") != std::wstring::npos &&
            code.find(L"zyJsonValue") == std::wstring::npos, "Profile marker and no unwanted JSON dependency");
        check((code.find(L"tray-menu-command") != std::wstring::npos) == (t!=0) &&
            (code.find(L"second-instance") != std::wstring::npos) == (single!=0), "Only selected events generated");
        check(code.find(L"App.初始化 (真, \"jade-app.log\", \"\", \"Jade应用\"") != std::wstring::npos &&
            code.find(L".如果真 (JadeView.App.初始化") != std::wstring::npos, "Initialization encoding and second-instance loop guard");
        check(code.find(L"ShowWindowAsync") == std::wstring::npos &&
            code.find(L"API_恢复窗口") == std::wstring::npos &&
            code.find(L"获取窗口句柄") == std::wstring::npos, "No native window restore dependency");
        check(code.find(L"v3;t=") != std::wstring::npos, "Changed template has a distinct revision");
        const auto start = code.find(L".子程序 Jade_公共_最小化,");
        const auto minimize = code.substr(start, code.find(L".子程序 ", start + 1) - start);
        check((minimize.find(L"设置窗口显示或隐藏 (窗口ID, 假)") != std::wstring::npos) == (t!=0) &&
            minimize.find(L"窗口.最小化 (窗口ID)") != std::wstring::npos,
            "Tray hides with ordinary minimize fallback; other profiles unchanged");
        if(argc>1) {
            const int n=WideCharToMultiByte(CP_UTF8,0,code.data(),static_cast<int>(code.size()),nullptr,0,nullptr,nullptr);
            std::string utf8(n,'\0');
            WideCharToMultiByte(CP_UTF8,0,code.data(),static_cast<int>(code.size()),utf8.data(),n,nullptr,nullptr);
            std::ofstream file("profile-"+std::to_string(t)+std::to_string(single)+".txt",std::ios::binary);
            file<<utf8; if(!file) return 2;
        }
    }
    const auto tray=BuildSource({true,true,L"jade-profile-test"});
    check(tray.find(L".局部变量 事件, 类_json") != std::wstring::npos &&
        tray.find(L"取属性数值 (\"tray_id\")") != std::wstring::npos &&
        tray.find(L"取通用属性 (\"key\", )") != std::wstring::npos, "Verified class_json interface");
    check(tray.find(L"InterlockedCompareExchange") != std::wstring::npos &&
        tray.find(L"设置窗口显示或隐藏 (窗口ID, 真) ＝ 假") != std::wstring::npos,
        "Preserve lifecycle guards and check module visibility result");
    check(tray.find(L".如果真 (文件是否存在 (图标路径) ＝ 假)") < tray.find(L"托盘.创建 ()"), "Missing icon cannot enable tray mode");
    check(tray.find(L"托盘.设置菜单项") < tray.find(L"Jade_公共_API_交换 (Jade_公共_托盘就绪, 1)"), "Menu ready before close interception");
    check(tray.find(L"真正退出 ()") != std::wstring::npos && tray.find(L"窗口.设置焦点 (窗口ID)") != std::wstring::npos, "Real exit and module focus");
    check(tray.find(L"double-click") != std::wstring::npos, "Tray single and double click show window");
    const auto showStart = tray.find(L".子程序 Jade_公共_显示主窗口\n");
    const auto show = tray.substr(showStart, tray.find(L".子程序 ", showStart + 1) - showStart);
    check(show.find(L"设置窗口显示或隐藏") < show.find(L"等待恢复, 0") &&
        show.find(L"等待恢复, 0") < show.find(L"设置焦点"), "Clear pending restore only after successful show");
    return passed==total ? 0 : 1;
}
