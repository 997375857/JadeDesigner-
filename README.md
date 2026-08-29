# JadeDesigner 内存桥接版

## 版本说明：内存桥接版

> 本分支是 **内存桥接版本**，不是 `main` 分支中的纯公开 IDE 接口版本。预览页和常规事件仍使用公开 IDE 接口；涉及程序集/代码写入的部分，会通过配套的 `jadehook.dll` 在运行中的 `e5.95.exe` 进程内完成桥接。
>
> `jadehook.dll` 不是本仓库的编译产物，需从配套的 `jadehook` 工程构建并与 `JadeHybrid.fne` 一起部署。仓库内部模块名保留为 `JadeHybrid`，用于兼容现有安装。

易语言 5.95（Win32/x86）IDE 插件：在代码区底部注入原生"Jade预览"页签，用 WebView2 实时渲染本地 HTML，
并把网页按钮、选择框、单选框/复选框事件**通过易语言公开 IDE 接口**自动写入工程——已有子程序直接跳转，
没有则自动创建程序集/子程序并改名定位。预览和事件定位使用公开 IDE API；代码写入由配套 `jadehook.dll` 通过内存桥接完成，不修改 `e5.95.exe` 文件本身。

- 当前版本：**内存桥接版 0.3（JadeHybrid / HOOK_FIXED_APPEND）**，目标链路已在 e5.95 实测通过
- 目标 IDE：`e5.95.exe`（SHA256 `368CBBD3...ABE1409`，其他版本未验证）
- WebView2 Runtime：≥ 151.0.4129.107（固定用户数据目录 `%TEMP%\JadeDesigner_WebView2_5_95`）

## 功能一览

| 能力 | 说明 |
| --- | --- |
| Jade预览 页签 | 精确定位主窗口直属 `CCustomTabCtrl`（控件 ID 59392），通过 `WM_MDICREATE` 建独立 MDI 文档页，与"程序集1、程序集2"等原生页签并列，互不遮挡 |
| 热刷新 | 修改 `lib\JadeDesigner\web\index.html` 保存后约 600ms 自动重载 |
| HTML 事件桥 | 网页事件以 8 段制表符消息 `JADE_EVT\tDOM事件\t控件类型\t控件ID\t值\t选中状态\t处理名\t程序集名` 回传 FNE |
| 事件路由 | 普通事件 → 程序集 `Jade注册事件`；处理名以 `ipc_`/`UI_` 开头 → 程序集 `UI_JadeView`；可用 `data-jade-handler` / `data-jade-assembly` 显式指定 |
| 代码写入 | 已有子程序 → `FN_MOVE_CARET` 跳转；没有 → 原生菜单插入程序集 + `FN_INSERT_NEW_SUB` 建子程序 + `FN_SET_AND_COMPILE_PRG_ITEM_TEXT` 改名；最终写入由 `jadehook.dll` 内存桥接完成 |
| 内存桥接 | `HookBridge` 动态加载配套 `jadehook.dll`，调用 `JadeHookGenerateAssembly` / `JadeHookInsertAnsi` 完成运行中 IDE 的程序集和代码写入 |

默认事件名映射：`按钮 → 控件ID_被单击`、`select → 控件ID_选择项被改变`、`radio/checkbox → 控件ID_选中状态被改变`。

## 目录结构

```text
JadeDesigner/
├── src/
│   ├── PluginEntry.cpp        # FNE 入口：GetNewInf / LIB_INFOX / 消息通知 / 版本
│   ├── IDEIntegration.cpp/.h  # 主窗口、59392 页签、MDI 客户区定位，子类化与菜单
│   ├── WebPreview.cpp/.h      # WebView2 环境/Controller、MDI 预览页、热刷新、WebMessage
│   ├── IdeEventRouter.cpp/.h  # 事件解析、程序树页面定位、代码页写入（核心）
│   ├── HookBridge.cpp/.h      # 通过 jadehook.dll 进行内存桥接写入
│   ├── DesignerLog.cpp/.h     # 日志（%E_LANG_HOME%\lib\JadeDesigner.log）
│   └── JadeDesigner.def       # 导出 GetNewInf
├── elib/                      # 易语言 FNE SDK 与公开 IDE 功能码定义（源自 AutoLinker 项目）
├── thirdparty/WebView2.h      # WebView2 SDK 头（Microsoft，见 THIRD_PARTY_NOTICES.md）
├── thirdparty/webview2/x86/   # WebView2LoaderStatic.lib
├── web/index.html             # 预览测试页（可随意改，保存即热刷新）
├── docs/
│   ├── E595_REVERSE_NOTES.md  # e5.95 逆向结果与收益对照（每个发现换来什么）
│   └── e595_静态分析_2026-08-28.md # 菜单命令表 / 12 个内部文本包函数特征码
├── build.ps1                  # 一键编译脚本
├── JadeDesigner.sln/.vcxproj  # Visual Studio 2026 (v145)，Release | Win32，C++20，MBCS
└── ARCHITECTURE.md            # 架构深档：接口原理、验证策略、踩坑记录（必读）
```

## 编译

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\build.ps1 *> build.log
```

产物：`bin\Release\JadeHybrid.fne`、`bin\Release\jadehook.dll` 与 `bin\Release\JadeDesigner\web\index.html`。其中 `jadehook.dll` 由配套 `jadehook` 工程提供。

## 安装与测试

1. 完全关闭易语言。
2. 准备与本插件匹配的 `jadehook.dll`；禁用/移走 `JadeDesignerProbe.fne`（避免日志混淆）；不要动 AutoLinker。
3. 复制 `JadeHybrid.fne`、`jadehook.dll` 与 `JadeDesigner\` 文件夹到 `%E_LANG_HOME%\lib\`。
4. 启动 `e5.95.exe`，支持库配置里启用 `JadeDesigner`。
5. 新建可丢弃工程 → 底部点"Jade预览" → 点页面里的"按钮1"。
6. 预期：程序树出现程序集 **Jade注册事件**（含 **按钮1_被单击**）；再点按钮1只跳转；
   改选择框会往**同一个** Jade注册事件 追加 `主题选择框_选择项被改变`；
   点"公共 UI：窗口最小化"会创建 **UI_JadeView.ipc_窗口最小化**。

日志：`%E_LANG_HOME%\lib\JadeDesigner.log`（启动第一行含版本号；关键标记见 ARCHITECTURE.md）。

## 开源前待办

- [ ] 确定开源协议并添加 LICENSE（当前未定）
- [ ] `elib/` 来自 [AutoLinker](https://github.com/aiqinxuancai/AutoLinker)，确认其协议兼容性并在 THIRD_PARTY_NOTICES.md 标明出处
- [x] 移除/泛化文档中的本机绝对路径
- [x] 初始化 git 仓库并推送到 GitHub（.gitignore 已备）

详细原理、e5.95 逆向证据与踩坑记录见 **[ARCHITECTURE.md](ARCHITECTURE.md)**。
