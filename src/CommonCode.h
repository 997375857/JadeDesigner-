#pragma once

#include <string>
#include <string_view>
#include <cstdint>

// Kept as source text so the generated EPL is reviewable and testable separately
// from the exact-build model importer. No user startup routine is replaced.
namespace CommonCode {
inline constexpr wchar_t Assembly[] = L"Jade_公共基础";
inline constexpr wchar_t SubscriptionAssembly[] = L"Jade_通讯_订阅集";
inline constexpr wchar_t SubscriptionRoutine[] = L"Jade_通讯_订阅";
inline constexpr wchar_t Source[] = LR"EPL(.版本 2
.程序集 Jade_公共基础

.子程序 Jade_公共_启动, 整数型

JadeView.App.注册事件 ("app-ready", &Jade_公共_准备就绪)
JadeView.App.注册事件 ("window-all-closed", &Jade_公共_全部窗口关闭)
JadeView.App.初始化 (真, "jade-app.log", "", GBK文本到UTF8文本 ("Jade应用"), "jade-app", 假)
JadeView.App.消息循环 ()
返回 (0)

.子程序 Jade_公共_准备就绪
.参数 成功否, 逻辑型
.参数 数据, 文本型

.如果 (成功否)
    JadeView.通讯.订阅 ("win:minimize", &Jade_公共_最小化)
    JadeView.通讯.订阅 ("win:maximize", &Jade_公共_最大化切换)
    JadeView.通讯.订阅 ("win:close", &Jade_公共_关闭窗口)
    Jade_通讯_订阅 ()
    Jade_公共_创建主窗口 ()
.否则
    信息框 ("JadeView初始化失败：" ＋ UTF8文本到GBK文本 (数据), 0, , )
    JadeView.App.退出 ()
.如果结束

.子程序 Jade_公共_创建主窗口
.局部变量 地址, 文本型
.局部变量 窗口ID, 整数型

地址 ＝ JadeView.协议服务.创建服务 (取运行目录 () ＋ "\web", 假)
窗口ID ＝ JadeView.窗口.创建 (地址, 0, Jade_公共_窗口设置 (), Jade_公共_视窗设置 ())
.如果真 (窗口ID ＝ 0)
    信息框 ("JadeView窗口创建失败，请检查 web 目录和运行环境。", 0, , )
    JadeView.App.退出 ()
.如果真结束

.子程序 Jade_公共_窗口设置, JadeView窗口设置
.局部变量 设置, JadeView窗口设置

设置.标题 ＝ GBK文本到UTF8文本 ("Jade应用")
设置.宽度 ＝ 1280
设置.高度 ＝ 800
设置.可调整大小边框 ＝ 真
设置.边框样式 ＝ 标题栏_标准标题栏_带边框
设置.最大化按钮 ＝ 真
设置.最小化按钮 ＝ 真
设置.隐藏窗口 ＝ 假
设置.透明背景 ＝ 假
设置.X坐标 ＝ -1
设置.Y坐标 ＝ -1
设置.焦点 ＝ 真
返回 (设置)

.子程序 Jade_公共_视窗设置, JadeView视窗设置
.局部变量 设置, JadeView视窗设置

设置.开启右键菜单 ＝ 假
返回 (设置)

.子程序 Jade_公共_全部窗口关闭
.参数 窗口ID, 整数型
.参数 数据, 文本型

JadeView.App.退出 ()

.子程序 Jade_公共_最小化, 整数型
.参数 窗口ID, 整数型
.参数 数据, 文本型

JadeView.窗口.最小化 (窗口ID)
返回 (0)

.子程序 Jade_公共_最大化切换, 整数型
.参数 窗口ID, 整数型
.参数 数据, 文本型

JadeView.窗口.最大化切换 (窗口ID)
返回 (0)

.子程序 Jade_公共_关闭窗口, 整数型
.参数 窗口ID, 整数型
.参数 数据, 文本型

JadeView.窗口.销毁 (窗口ID)
返回 (0)
)EPL";

struct Options {
    bool tray = false;
    bool singleInstance = false;
    std::wstring appId;
};

inline bool ValidAppId(std::wstring_view id) {
    if (id.size() < 6 || id.size() > 64) return false;
    for (wchar_t c : id)
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
            (c >= L'0' && c <= L'9') || c == L'-' || c == L'_')) return false;
    return true;
}

inline std::wstring ProjectAppId(std::wstring_view path) {
    if (path.empty()) return {};
    uint64_t hash = 14695981039346656037ull;
    for (wchar_t c : path) {
        if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
        if (c == L'/') c = L'\\';
        for (unsigned shift : {0u, 8u}) { hash ^= (c >> shift) & 255; hash *= 1099511628211ull; }
    }
    std::wstring out = L"jade-";
    for (int i = 60; i >= 0; i -= 4) out += L"0123456789abcdef"[(hash >> i) & 15];
    return out;
}

// Strict wire grammar: browser input cannot inject EPL source or extra commands.
inline bool ParseCommand(std::wstring_view wire, Options& options) {
    options = {};
    if (wire == L"JADE_COMMAND\tgenerate_common") return true;
    constexpr std::wstring_view prefix = L"JADE_COMMAND\tgenerate_common\t";
    if (wire.substr(0, prefix.size()) != prefix) return false;
    const auto fields = wire.substr(prefix.size());
    if (fields.size() < 4 || fields[1] != L'\t' || fields[3] != L'\t' ||
        (fields[0] != L'0' && fields[0] != L'1') ||
        (fields[2] != L'0' && fields[2] != L'1')) return false;
    options.tray = fields[0] == L'1';
    options.singleInstance = fields[2] == L'1';
    options.appId = fields.substr(4);
    return options.appId.empty() || ValidAppId(options.appId);
}

inline constexpr wchar_t Lifecycle[] = LR"EPL(
.子程序 Jade_内部_读状态, 整数型
.参数 状态, 整数型, 参考

返回 (状态)

.子程序 Jade_内部_真正退出

.如果真 (Jade_内部_退出中 ≠ 0)
    返回 ()
.如果真结束
Jade_内部_退出中 ＝ 1
Jade_内部_清理托盘 ()
JadeView.App.退出 ()

.子程序 Jade_内部_显示主窗口
.局部变量 窗口ID, 整数型

.如果真 (Jade_内部_读状态 (Jade_内部_退出中) ≠ 0)
    返回 ()
.如果真结束
Jade_内部_等待恢复 ＝ 1
窗口ID ＝ Jade_内部_读状态 (Jade_内部_主窗口ID)
.如果真 (窗口ID ＝ 0)
    返回 ()
.如果真结束
.如果真 (JadeView.窗口.设置窗口显示或隐藏 (窗口ID, 真) ＝ 假)
    返回 ()
.如果真结束
Jade_内部_等待恢复 ＝ 0
JadeView.窗口.设置焦点 (窗口ID)

.子程序 Jade_公共_窗口创建完毕
.参数 窗口ID, 整数型
.参数 数据, 文本型

.如果真 (窗口ID ＝ Jade_内部_读状态 (Jade_内部_主窗口ID) 且 Jade_内部_读状态 (Jade_内部_等待恢复) ≠ 0)
    Jade_内部_显示主窗口 ()
.如果真结束

.子程序 Jade_公共_窗口已关闭
.参数 窗口ID, 整数型
.参数 数据, 文本型

.如果真 (窗口ID ≠ 0 且 窗口ID ＝ Jade_内部_主窗口ID)
    Jade_内部_主窗口ID ＝ 0
    Jade_内部_真正退出 ()
.如果真结束
)EPL";

inline constexpr wchar_t TraySource[] = LR"EPL(
.子程序 Jade_内部_注册托盘事件, 逻辑型

.如果真 (JadeView.App.注册事件 ("window-closing", &Jade_公共_窗口关闭前) ＝ 0)
    返回 (假)
.如果真结束
.如果真 (JadeView.App.注册事件 ("tray-event", &Jade_公共_托盘点击) ＝ 0)
    返回 (假)
.如果真结束
.如果真 (JadeView.App.注册事件 ("tray-menu-command", &Jade_公共_托盘菜单) ＝ 0)
    返回 (假)
.如果真结束
返回 (真)

.子程序 Jade_内部_创建托盘
.局部变量 托盘ID, 整数型
.局部变量 菜单, 托盘菜单选项, , "2"
.局部变量 图标路径, 文本型
.局部变量 解析检查, 类_json

.如果真 (解析检查.解析 ("{}", 真, ) ＝ 假)
    信息框 ("托盘未启用：类_json 初始化失败。本次关闭窗口将正常退出。", 0, , )
    返回 ()
.如果真结束
图标路径 ＝ 取运行目录 () ＋ "\app.ico"
.如果真 (文件是否存在 (图标路径) ＝ 假)
    信息框 ("托盘未启用：请在程序目录放置 app.ico。本次关闭窗口将正常退出。", 0, , )
    返回 ()
.如果真结束
托盘ID ＝ JadeView.托盘.创建 ()
.如果真 (托盘ID ＝ 0)
    返回 ()
.如果真结束
Jade_内部_托盘ID ＝ 托盘ID
菜单 [1].菜单项类型 ＝ 0
菜单 [1].菜单项Key ＝ "jade_common_show"
菜单 [1].菜单项显示文本 ＝ GBK文本到UTF8文本 ("显示主窗口")
菜单 [2].菜单项类型 ＝ 0
菜单 [2].菜单项Key ＝ "jade_common_exit"
菜单 [2].菜单项显示文本 ＝ GBK文本到UTF8文本 ("退出程序")
菜单 [2].是否标记为危险操作 ＝ 真
.如果真 (JadeView.托盘.设置图标 (托盘ID, 图标路径) ＝ 假)
    Jade_内部_清理托盘 ()
    返回 ()
.如果真结束
.如果真 (JadeView.托盘.设置菜单项 (托盘ID, 菜单) ＝ 假)
    Jade_内部_清理托盘 ()
    返回 ()
.如果真结束
JadeView.托盘.设置提示文本 (托盘ID, "Jade应用")
.如果真 (JadeView.托盘.显示图标 (托盘ID) ＝ 假)
    Jade_内部_清理托盘 ()
    返回 ()
.如果真结束
.如果真 (Jade_内部_读状态 (Jade_内部_退出中) ≠ 0)
    Jade_内部_清理托盘 ()
    返回 ()
.如果真结束
Jade_内部_托盘就绪 ＝ 1

.子程序 Jade_内部_清理托盘
.局部变量 托盘ID, 整数型

Jade_内部_托盘就绪 ＝ 0
托盘ID ＝ Jade_内部_托盘ID
Jade_内部_托盘ID ＝ 0
.如果真 (托盘ID ≠ 0)
    JadeView.托盘.销毁 (托盘ID)
.如果真结束

.子程序 Jade_公共_窗口关闭前, 整数型
.参数 窗口ID, 整数型
.参数 数据, 文本型

' 同步关闭拦截：不等待、不弹框，仅在托盘与恢复入口均可用时隐藏主窗口。
.如果真 (Jade_内部_读状态 (Jade_内部_退出中) ≠ 0 或 窗口ID ≠ Jade_内部_读状态 (Jade_内部_主窗口ID))
    返回 (0)
.如果真结束
.如果真 (Jade_内部_读状态 (Jade_内部_托盘就绪) ＝ 0)
    返回 (0)
.如果真结束
.如果真 (JadeView.窗口.设置窗口显示或隐藏 (窗口ID, 假))
    返回 (1)
.如果真结束
返回 (0)

.子程序 Jade_公共_托盘点击
.参数 窗口ID, 整数型
.参数 数据, 文本型
.局部变量 事件, 类_json

.如果真 (事件.解析 (UTF8文本到GBK文本 (数据), 真, ) ＝ 假)
    Jade_内部_显示主窗口 ()
    Jade_内部_清理托盘 ()
    返回 ()
.如果真结束
.如果真 (Jade_内部_读状态 (Jade_内部_托盘就绪) ＝ 0 或 事件.取属性数值 ("tray_id") ≠ Jade_内部_读状态 (Jade_内部_托盘ID))
    返回 ()
.如果真结束
.如果真 (事件.取通用属性 ("event", ) ＝ "left-click" 或 事件.取通用属性 ("event", ) ＝ "double-click")
    Jade_内部_显示主窗口 ()
.如果真结束

.子程序 Jade_公共_托盘菜单
.参数 窗口ID, 整数型
.参数 数据, 文本型
.局部变量 事件, 类_json

.如果真 (事件.解析 (UTF8文本到GBK文本 (数据), 真, ) ＝ 假)
    Jade_内部_显示主窗口 ()
    Jade_内部_清理托盘 ()
    返回 ()
.如果真结束
.如果真 (Jade_内部_读状态 (Jade_内部_托盘就绪) ＝ 0 或 事件.取属性数值 ("tray_id") ≠ Jade_内部_读状态 (Jade_内部_托盘ID))
    返回 ()
.如果真结束
.判断开始 (事件.取通用属性 ("key", ) ＝ "jade_common_show")
    Jade_内部_显示主窗口 ()
.判断 (事件.取通用属性 ("key", ) ＝ "jade_common_exit")
    Jade_内部_真正退出 ()
.默认
.判断结束
)EPL";

inline std::wstring BuildSource(const Options& options) {
    if (!ValidAppId(options.appId)) return {};
    std::wstring s = Source;
    // Normalize once; fragment replacements never depend on checkout EOL style.
    for (size_t p = 0; (p = s.find(L'\r', p)) != std::wstring::npos;) s.erase(p, 1);
    const auto replace = [&](std::wstring_view old, std::wstring_view value) {
        const auto p = s.find(old);
        if (p != std::wstring::npos) s.replace(p, old.size(), value);
    };
    replace(L"GBK文本到UTF8文本 (\"Jade应用\"), \"jade-app\", 假",
        L"\"Jade应用\", \"" + options.appId + L"\", " + (options.singleInstance ? L"真" : L"假"));
    // init=false also denotes an already-running first instance: do not enter a
    // second message loop, nor call global shutdown against the first instance.
    replace(L"JadeView.App.初始化 (", L".如果真 (JadeView.App.初始化 (");
    replace(options.singleInstance ? L"\", 真)\nJadeView.App.消息循环 ()" : L"\", 假)\nJadeView.App.消息循环 ()",
        std::wstring(L"\", ") + (options.singleInstance ? L"真" : L"假") + L"))\n    JadeView.App.消息循环 ()\n.如果真结束");
    s += L"\n.子程序 Jade_公共_生成配置, 文本型\n\n返回 (\"v3;t=" +
        std::wstring(options.tray ? L"1" : L"0") + L";s=" + (options.singleInstance ? L"1" : L"0") +
        L";id=" + options.appId + L"\")\n";
    if (!options.tray && !options.singleInstance) return s;
    replace(L".程序集 Jade_公共基础\n", LR"EPL(.程序集 Jade_公共基础
.程序集变量 Jade_内部_主窗口ID, 整数型
.程序集变量 Jade_内部_退出中, 整数型
.程序集变量 Jade_内部_等待恢复, 整数型
.程序集变量 Jade_内部_启动过, 整数型
.程序集变量 Jade_内部_托盘ID, 整数型
.程序集变量 Jade_内部_托盘就绪, 整数型
)EPL");
    replace(L"JadeView.App.注册事件 (\"app-ready\"", LR"EPL(.如果真 (Jade_内部_启动过 ≠ 0)
    返回 (0)
.如果真结束
Jade_内部_启动过 ＝ 1
JadeView.App.注册事件 ("window-created", &Jade_公共_窗口创建完毕)
JadeView.App.注册事件 ("window-closed", &Jade_公共_窗口已关闭)
JadeView.App.注册事件 ("app-ready")EPL");
    replace(L".如果真 (窗口ID ＝ 0)", L"Jade_内部_主窗口ID ＝ 窗口ID\n.如果真 (Jade_内部_读状态 (Jade_内部_等待恢复) ≠ 0)\n    Jade_内部_显示主窗口 ()\n.如果真结束\n.如果真 (窗口ID ＝ 0)");
    // All exit sites funnel through a guarded, real shutdown.
    size_t p = 0;
    while ((p = s.find(L"JadeView.App.退出 ()", p)) != std::wstring::npos) {
        s.replace(p, std::wstring_view(L"JadeView.App.退出 ()").size(), L"Jade_内部_真正退出 ()"); ++p;
    }
    s += Lifecycle;
    if (options.singleInstance) {
        replace(L"JadeView.App.注册事件 (\"app-ready\"", L"JadeView.App.注册事件 (\"second-instance\", &Jade_公共_重复启动)\nJadeView.App.注册事件 (\"app-ready\"");
        s += LR"EPL(
.子程序 Jade_公共_重复启动
.参数 窗口ID, 整数型
.参数 数据, 文本型

Jade_内部_显示主窗口 ()
)EPL";
    }
    if (options.tray) {
        // Only a ready tray can replace the page's minimize action with hiding.
        replace(L"JadeView.窗口.最小化 (窗口ID)", LR"EPL(.如果真 (窗口ID ＝ Jade_内部_读状态 (Jade_内部_主窗口ID) 且 Jade_内部_读状态 (Jade_内部_托盘就绪) ≠ 0)
    .如果真 (JadeView.窗口.设置窗口显示或隐藏 (窗口ID, 假))
        返回 (0)
    .如果真结束
.如果真结束
JadeView.窗口.最小化 (窗口ID))EPL");
        replace(L"    Jade_公共_创建主窗口 ()", L"    Jade_公共_创建主窗口 ()\n    .如果真 (Jade_内部_读状态 (Jade_内部_退出中) ＝ 0)\n        .如果真 (Jade_内部_注册托盘事件 ())\n            Jade_内部_创建托盘 ()\n        .如果真结束\n    .如果真结束");
        s += TraySource;
    } else {
        s += L"\n.子程序 Jade_内部_清理托盘\n\n";
    }
    return s;
}
} // namespace CommonCode
