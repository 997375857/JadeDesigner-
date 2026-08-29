# JadeDesigner 内存桥接版架构深档（JadeHybrid 0.3）

更新：2026-08-28。本文记录插件每一部分"怎么做到的"、支撑它的 e5.95 逆向证据、
以及踩过的全部坑——后续维护或移植到其他易语言版本前必读。

> **分支说明：内存桥接**：本分支的程序集/代码写入不是完全依赖公开 IDE API，而是由 `HookBridge` 动态加载配套 `jadehook.dll`，调用 `JadeHookGenerateAssembly` 与 `JadeHookInsertAnsi` 在运行中的 IDE 进程内完成。`jadehook.dll` 不随本仓库提交。

---

## 1. 总体架构

```text
┌─────────────────────────── e5.95.exe 进程 ───────────────────────────┐
│  主窗口 ENewFrame                                                     │
│   ├─ 菜单/工具条                                                       │
│   ├─ MDIClient (id 59648)                                             │
│   │    ├─ MDI 文档"程序集: Jade注册事件"（代码页网格）                   │
│   │    ├─ MDI 文档"窗口: _启动窗口"                                     │
│   │    └─ MDI 文档"Jade预览" ← WebPreview 创建                          │
│   └─ CCustomTabCtrl (id 59392) ← 底部页签（插件注入"Jade预览"标签）       │
│                                                                        │
│  JadeHybrid.fne（本插件，内部模块名保留 JadeHybrid）                                            │
│   ├─ PluginEntry   GetNewInf / NL_* 消息 → 附加 IDE                     │
│   ├─ IDEIntegration 找主窗口/页签/MDI 客户区，注入标签与菜单              │
│   ├─ WebPreview    WebView2 环境 → Controller → 预览 MDI 页              │
│   │      ▲ WebMessageReceived（事件上行） / ExecuteScript（桥安装）      │
│   ├─ IdeEventRouter 接收 JADE_EVT → 定位/创建 程序集+子程序 → 跳转        │
│   └─ HookBridge 动态加载 jadehook.dll → 内存桥接程序集/代码写入       │
│          │ 全部通过 NotifySys(NES_RUN_FUNC, FN_xxx, 参数) 公开接口        │
└────────────────────────────────────────────────────────────────────────┘
```

### 模块职责

| 文件 | 职责 |
| --- | --- |
| `PluginEntry.cpp` | 导出 `GetNewInf`；处理 `NL_IDE_READY`/`NL_SYS_NOTIFY_FUNCTION`/`NL_UNLOAD_FROM_IDE`；注册"Toggle/Refresh Jade Preview"插件功能；日志首行打版本号 |
| `IDEIntegration.cpp` | 枚举定位主窗口直属 `CCustomTabCtrl`（59392）与 MDIClient（59648）；启动/停止集成 |
| `WebPreview.cpp` | WebView2 `CreateCoreWebView2Environment` → `CreateCoreWebView2Controller`，宿主到自建 `JadeDesigner.PreviewMdiChild` MDI 子窗口；文件时间戳轮询实现 600ms 热刷新；`AddWebMessageReceived` 收事件；页面加载后 ExecuteScript 安装 `install_ui_event_bridge` |
| `IdeEventRouter.cpp` | 解析 8 段制表符消息；按程序集名定位/创建 MDI 页；在代码页网格上读写；创建/改名/跳转子程序；全部写入走公开 IDE 功能接口 |
| `DesignerLog.cpp` | 追加写 `lib\JadeDesigner.log`，带毫秒时间戳 |

## 2. HTML 事件桥协议（1.6.13 起为 10 段）

页面 → 插件（`window.chrome.webview.postMessage`）：

```text
JADE_EVT \t domEvent \t controlType \t elementId \t value \t checked \t handlerName \t assemblyName \t callType \t callParam
```

- 全部字段 URL 百分号编码（UTF-8），插件侧 `PercentDecodeUtf8` 解码（兼容 8 段旧消息）。
- HTML 可用 `data-jade-handler` / `data-jade-assembly` 显式指定处理名与程序集；
  未指定时按控件类型生成默认处理名，普通事件程序集默认 `Jade通讯注册事件`。
- **callType 用于生成 JadeView 调用语句（1.6.13）**：

| HTML 属性 | 生成的易语言代码 | 写入位置（固定程序集/固定子程序） |
| --- | --- | --- |
| `data-jade-call="JadeView.通讯.订阅"` + `data-jade-channel="win:minimize"` | `JadeView.通讯.订阅 ("win:minimize", &ipc_窗口最小化)` | `Jade_通讯_订阅集` → `Jade_通讯_订阅` |
| `data-jade-call="JadeView.App.注册事件"` + `data-jade-event="应用准备就绪"` | `JadeView.App.注册事件 (#事件_应用准备就绪, &UI_就绪回调)` | `UI_启动JadeView` → `UI_启动JadeView` |

- 固定子程序里**先扫描语句区**：检测标记（如 `JadeView.通讯.订阅(`+通道）已存在则不动，只跳转；
  不存在才在子程序末尾用 `FN_INSERT_TEXT` 追加（光标落点在最后一个语句行，逐行回读验证）。
- **初始化模板**：UI_启动JadeView 固定子程序若不含 `JadeView.App.初始化`，
  自动在末尾追加初始化 + 消息循环模板（应用名/应用唯一标识留空 `""`，已有则完全不动）：
  `初始化成功 ＝ JadeView.App.初始化 (假, "", 取运行目录 (), "", "", 假)` + 失败提示 +
  `JadeView.App.消息循环 ()`。
- 回调子程序（& 后面的名字）走普通路由规则：`ipc_`/`UI_` 开头 → `UI_JadeView`，否则 → `Jade通讯注册事件`。
- ⚠️ `JadeView.*` 命令可编译的前提是支持库已启用（`.支持库 spec` 为文件头声明，网格层无法安全写入）：
  若编译提示未知命令，请在支持库配置中启用对应支持库。
- 处理名以 `ipc_` / `UI_` 开头时强制路由到 `UI_JadeView`。
- 桥安装（`install_ui_event_bridge`）在 `NavigationCompleted` 后用 `ExecuteScript` 注入，
  对 `click`/`change` 做捕获监听后组包上报。

## 3. e5.95 逆向证据（e5.95.exe，SHA256 368CBBD3...ABE1409，ImageBase 0x400000）

> 完整的"发现 → 收益"对照、方法论（capstone 常量扫描 + 调用点反汇编 + 行为对照）
> 与复现脚本见 **[docs/E595_REVERSE_NOTES.md](docs/E595_REVERSE_NOTES.md)**；
> 菜单命令表与 12 个内部文本包函数特征码见 `docs/e595_静态分析_2026-08-28.md`。
> 本节只保留插件实际依赖的结论。

以下 VA 为首选基址地址，插件运行时应以 `GetModuleHandle(nullptr)` 实际基址换算；
本插件最终**只依赖公开功能码**，这些地址仅作为"为什么这样调用有效"的证据保留。

| 功能 | VA | 说明 |
| --- | ---: | --- |
| `FN_GET_PRG_TEXT` 处理器 | `0x004C4D64` | 操作当前编辑器对象（光标行/列在对象 `+0x74`/`+0x78`）；调用 `text_buffer_init(0x4918B0)`；**永远返回 1（成功）**，越界时 type=0、BufSize=1 |
| 名称单元格提交路径 | `0x0044D0B0` | IDE 自身改名时 `push 0x02020071`（即 `FN_SET_AND_COMPILE_PRG_ITEM_TEXT`）——这是"改名走该接口"的直接证据 |
| 键盘 `.` 输入处理 | `0x004C2290` | 编辑器处理 `0x2E` 字符时内部调用 `FN_INSERT_TEXT(0x0202006D)`——插入文本与键盘输入同一解析管道 |
| 设置活动编辑器 | `0x0047ADF0` | `__thiscall(host, editor, notifyMode)`，更新 `host+0x464`（AutoLinker 5.95 profile 一致） |
| 12 个内部文本包函数 | 见静态分析记录 | AutoLinker 的 5.95 特征全部逐字节命中；本插件未使用（公开接口已足够） |

代码页网格类型常量（实测）：程序集名 `VT_MOD_NAME=580`；子程序名块 `VT_SUB_NAME=686`
（每个子程序块由一行 ti=1 表头 + 一行 ti=0 名称行组成）；语句行 `t=851`。
新程序集页布局：`r0` 全表头，`r1c0` = 程序集名。

## 4. 五个核心原语（IdeEventRouter 的全部写入能力）

### 4.1 读单元格：`FN_GET_PRG_TEXT`（0x0503000A）

两段式：先 `m_pBuf=NULL` 拿 `m_nBufSize`（= strlen+1），再给缓冲区拿文本。
**坑：处理器永远返回 TRUE**——越界行也"成功"，但 type=0、reportedSize=1。
所以行存在性必须用 `CellHasData()`：`type != 0 || isTitle || reportedSize > 1 || 非空文本`。

### 4.2 移动光标并验证：`FN_MOVE_CARET`（0x01010042）+ `FN_GET_CARET_ROW_INDEX/COL_INDEX`

`NES_RUN_FUNC` 对无效行也返回 TRUE，所以移动后必须读回光标行列比对
（`QueryCaret`）。不匹配（如目标行不存在被 IDE 吸附到相邻行）时换路径。
`FN_MOVE_BOTTOM`（0x01010004）用于"到底部"，`FN_MOVE_EDIT_CARET_TO_END`（0x01010039）行尾。

### 4.3 创建子程序：`FN_INSERT_NEW_SUB`（0x02020009）→ 改名

新程序集页没有尾随空行，直接 `FN_INSERT_TEXT` 无处落脚（实测 `subs 0->0`）。
正确做法：`FN_MOVE_BOTTOM` → `FN_INSERT_NEW_SUB`（IDE 自己的"插入子程序"，产生模板"子程序N"）
→ 扫描新增的 `VT_SUB_NAME` 单元格 → `RenameCellAt()` 改成目标名。
已有尾随语句行的页面可走 `FN_INSERT_TEXT(".子程序 名\r\n")` 直接建（与键盘输入同管道，AutoLinker 同款）。

### 4.4 改名任意标题单元格：`FN_SET_AND_COMPILE_PRG_ITEM_TEXT`（0x02020071）

语义是"设置**当前光标所处行列**单元格的整体内容"。流程：
`FN_MOVE_CARET(行,列)` →（验证光标到位）→ 传 GBK 文本（仅名称本身，不带 `.程序集` 前缀）→
回读该单元格验证。**0.9 版失败的原因就是没先移光标**。MDI 标题做兜底验证
（标题变为 `程序集: Jade注册事件` 即成功，不依赖文本回读）。

### 4.5 创建程序集：原生菜单 32782 + MDI 子窗口快照对比

`WM_COMMAND(32782)`（= 插入新程序集，内部即 `FN_INSERT_NEW_MOD`）会创建并激活新代码页。
**新页检测不能用 `WM_MDIGETACTIVE`**——IDE 视觉标签层会干扰客户区活动状态（1.6.7 实测翻车）。
正确做法：命令前 `EnumChildWindows` 快照 MDI 子窗口集合，命令后 diff 出新增的子窗口（1.6.8 起），
再显式 `WM_MDIACTIVATE` + 泵消息稳定焦点。

## 5. 验证策略（公开接口返回值不可信）

`NES_RUN_FUNC` 返回"功能被处理"，不等于"生效"。所有写操作必须二次取证：

| 操作 | 取证方式 |
| --- | --- |
| 建子程序 | `VT_SUB_NAME` 单元格计数增加 + 精确名称匹配 |
| 改名（程序集/子程序） | 单元格文本回读精确匹配；程序集另加 MDI 标题兜底 |
| 新程序集页 | MDI 子窗口快照 diff + `GetWindowText` 标题 |
| 光标移动 | `FN_GET_CARET_ROW_INDEX/COL_INDEX` 读回比对 |

另有防重复保护：文本回读整体异常（`text_healthy=0`）且无法确认子程序存在时**拒绝插入**，
宁可停止也不重复创建；`Route()` 带原子重入锁，连点不并发写页。

## 6. 已放弃的路线（踩坑记录，勿重蹈）

1. **剪贴板整页粘贴**（0.8–0.11 早期）：能写入子程序但 **e5.95 忽略粘贴文本中的 `.程序集` 名称**，
   改名永远失败；且粘贴后紧跟的全选/复制命令会随机丢失（剪贴板序号不变造成"回声假成功"）。
   1.6.6 起彻底移除剪贴板。
2. **`FN_SET_AND_COMPILE_PRG_ITEM_TEXT` 不移光标直接调用**（0.9）：返回 TRUE 但不生效。
3. **`WM_MDIGETACTIVE` 轮询检测新页**（0.11–1.6.7）：视觉层干扰，检测不到新文档（1.6.7 日志
   `maximize_document` 已出现新 hwnd 但轮询 30 次全失败）。
4. **等待循环里泵消息**（1.6.7）：泵送会干扰 MDI 激活判定；等待用 `Sleep`，泵只用于命令间稳定。
5. **`FN_INSERT_TEXT` 落点不验证**（1.6.7）：新页无尾随行，插入无效还可能写脏名称单元格；
   1.6.9 起落点必须经光标读回确认。
6. **SendInput 模拟 F2/Ctrl+A/输入/回车 的单元格改名**（1.6.5）：手工可行但插件内时序脆弱，
   且会抢用户键盘；被 4.4 的纯 API 方案取代。

## 7. 日志标记速查（`%E_LANG_HOME%\lib\JadeDesigner.log`）

| 标记 | 含义 |
| --- | --- |
| `GetNewInf called; JadeDesigner 1.6.9 (...)` | 版本确认（排查"加载了旧版"第一看这里） |
| `PREVIEW ui_event_bridge_build=5 native_menu_page_edit=1` | WebView2 与事件桥就绪 |
| `UI_EVENT received ...` | 收到网页事件及解析结果 |
| `UI_EVENT new_document_activated ... activated=` | 新程序集页检测+显式激活 |
| `UI_EVENT page_state ... name_cell="..." text_healthy=` | 当前页名称单元格与文本回读健康度 |
| `UI_EVENT page_dump r0c0{t=,ti=,n=,x=} ...` | 网格转储（类型/标题/长度/文本），布局疑难用它 |
| `UI_EVENT create_sub path= / new_sub_cmd subs A->B` | 子程序创建路径与计数验证 |
| `UI_EVENT rename_cell set= verify= read_back="..."` | 单元格改名（子程序/程序集共用）及回读 |
| `UI_EVENT rename_api cell_renamed= title_verified=` | 程序集改名双路验证结果 |
| `UI_EVENT routed action= jump=` / `UI_EVENT result success=` | 路由结果终值 |

## 8. 版本沿革（要点）

| 版本 | 关键变化 |
| --- | --- |
| 0.7–0.10 | 原生 MDI 预览页、页签注入、热刷新、事件桥；`FN_INSERT_NEW_MOD`+`VT_MOD_NAME` 改名失败（程序集12~15 事故） |
| 0.11 | 转向原生菜单+复制/粘贴整页编辑；粘贴无法改程序集名，回滚机制止损 |
| 1.6.5 | 实测确诊：粘贴不改名、单元格编辑可改名、剪贴板回声假成功；引入 SendInput 改名（后被取代） |
| 1.6.6 | 逆向 e5.95 确认公开接口即 IDE 内部路径；全 API 化，零剪贴板 |
| 1.6.7 | `page_dump` 诊断；发现 NES_RUN_FUNC 对越界返回 TRUE |
| 1.6.8 | 新页检测改 MDI 子窗口快照 diff |
| 1.6.9 | matchAnyText 空表头遮蔽 bug 修复；`FN_INSERT_NEW_SUB`+改名双路径；光标读回验证——**全链路实测通过** |

## 9. 编译/发布注意事项

- VS2026 工具集 `v145`，**必须 Release | Win32**（e5.95 是 32 位），C++20，`/MBCS`。
- `thirdparty/webview2/x86/WebView2LoaderStatic.lib` 静态链接，发布单文件 FNE 即可，无需带 Loader DLL。
- FNE 版本号在 `PluginEntry.cpp` 的 `BuildLibraryInfo()`（m_nBuildNumber）与日志首行同步更新。
- 部署：`JadeDesigner.fne` + `JadeDesigner\web\index.html` 一起放进 `易语言目录\lib\`；
  换版本时把 FNE 和整个 `JadeDesigner` 文件夹同时替换。
- WebView2 用户数据目录固定 `%TEMP%\JadeDesigner_WebView2_5_95`（多实例共享，勿删运行中的）。
