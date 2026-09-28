# examples — JadeView 侧工程与运行时

配合 JadeHybrid 支持库联调用的 JadeView 侧文件，原样取自作者发布目录，未做任何修改。

| 文件 | 大小 (字节) | SHA256 | 说明 |
| --- | --- | --- | --- |
| `JadeView_v2.4.0-beta.1.26H01.e` | 705941 | `42638F5D71D6EBE4BF71AB9BFD70713D81F12F9D89BD72DD660F513C551AF5B6` | 易语言工程源码，文件头 `CNWTEPRG` |
| `JadeView_x64.dll` | 4043776 | `7BCB636EDD9E8BD69C71B9D3AF845DE51585D09BC3F016AAC49B0E4F67E9ED59` | JadeView 运行时，x64 |

## `.e` 工程里包含什么

对工程文件做可读串扫描，可以确认它承载的是 JadeView 的易语言侧声明与事件层：

- **命令声明**：129 处 `JadeView_x86.dll` 导入声明，前缀 `_JadeView_`；参数打包涉及 `window_id`、`buffer_size`、`ArrPtr`/`ArrLen`、`KeyPtr`/`KeyLen`、`ValPtr`/`ValLen`、`Base64`。
- **WebView2 事件**：`webview-did-start-loading`、`webview-did-finish-load`、`webview-download-completed`、`webview-page-favicon-updated`、`webview-page-title-updated`。
- **托盘事件**：`tray-event`。
- **控件身份字段**：`data-jade-id`（与 JadeHybrid 的控件身份规范同一套约定）。
- **JAPK 资源包错误码**：`JADEVIEW_JAPK_OK` 及 `..._INVALID_PARAM`、`..._NOT_INITIALIZED`、`..._LOAD_FAILED`、`..._INVALID_FORMAT`、`..._INVALID_SIGNATURE`、`..._APP_MISMATCH`、`..._DECRYPT_FAILED`、`..._UNSIGNED_NOT_ALLOWED`、`..._MISSING_PUBLIC_KEY`、`..._INVALID_PUBLIC_KEY`、`..._POLICY_DENIED`、`..._NOT_LOADED`，以及 WebView2 数据目录模板 `` `{data_directory}\EBWebView` ``。

## 使用

1. 用易语言打开 `JadeView_v2.4.0-beta.1.26H01.e`。
2. 按目标程序位数放置对应运行时：工程内声明的是 **`JadeView_x86.dll`**，而本目录附带的是 **`JadeView_x64.dll`**。32 位易语言程序用 x86 运行时；x64 目标需要把声明中的 DLL 名一并改掉，不能只换文件。
3. 网页资源按仓库 `web/index.html` 的组织方式放在工程目录下。

## 两处版本不一致，联调前先确认

| 项 | 标称 | 实际 |
| --- | --- | --- |
| 工程文件名 | `2.4.0-beta.1.26H01` | — |
| `JadeView_x64.dll` 文件版本 | — | `2.4.2.26I01`（`ProductName: JadeView`，`CompanyName: JadeView Team`，`FileDescription: JadeView WebView`） |

工程名与运行时的构建批次不同。联调时以实际加载到的 DLL 版本为准；要严格对齐，替换为同名版本的 JadeView 运行时。

## 相关

- 控件身份与网页侧对接约定：`docs/AI生成UI与JadeView支持库对接规范.md`
- 内存桥与支持库配套关系：`README.md`（内存 API v3，`JadeHybrid.fne` 与 `jadehook.dll` 必须配套）
