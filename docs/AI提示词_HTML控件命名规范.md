# AI 提示词：写给 JadeView 易语言支持库的 HTML 控件命名规范

> 本文是早期的命名参考。涉及控件动作、数据绑定、事件通道和实际运行时适配时，以最新版 [AI 生成 UI 与 JadeView 支持库对接规范](AI生成UI与JadeView支持库对接规范.md) 为准。HTML 属性本身不会自动发送通信，必须同时实现页面 JavaScript 适配层。

把下面 `====` 之间的全部内容，粘贴给任何 AI（Claude / Cursor / GPT 都行），
再跟上你的需求（"帮我写一个账号管理页面" 之类）。它生成的 HTML 就能让
JadeDesigner 插件在易语言里直接生成中文子程序名。

====

## 你要遵守的约定

我在用一个叫 **JadeView** 的易语言（EPL）支持库 + JadeDesigner IDE 插件做界面。
工作方式是：网页在易语言 IDE 里预览，我**在预览里点一下某个控件**，插件就自动在
易语言工程里创建对应的回调子程序并跳过去。子程序的名字直接取自 HTML，
所以 HTML 怎么写，决定了我在易语言里看到的是 `btn_import_accounts_被单击`
还是 `导入账号_被单击`。请按下面的规则写。

### 一、核心规则：给每个业务控件加 `data-jade-handler`

```html
<button id="btn-import-accounts" data-jade-handler="导入账号_被单击">导入账号</button>
```

- `id` **保持英文 kebab-case 不要动**：CSS 和 JS 要用它，插件在没有显式通道时也用它
  推导订阅通道 `ui:<id>`，改成中文会让运行时订阅对不上。
- `data-jade-handler` 写**完整的中文子程序名，含事件后缀**，这是我在易语言里唯一
  会看到的名字，要一眼看懂这个按钮干什么。

事件后缀只有这三个，必须原样使用：

| 控件 | 触发事件 | 后缀 |
|---|---|---|
| `<button>` / `[role=button]` / `<input type=button\|submit>` | click | `_被单击` |
| `<input type=checkbox>` / `<input type=radio>` | change | `_选中状态被改变` |
| `<select>` | change | `_选择项被改变` |

### 二、哪些控件会生成代码，哪些不会

**会生成**：只有上表这几类。点击事件是用 `closest()` 匹配的，所以
`<button><i class="fa fa-plus"></i> 新建</button>` 里点到图标也算点到按钮。

**不会生成**（写了也没用，别指望）：`<a>`、`<div>`（除非加 `role="button"`）、
文本框 `<input type=text>`、`<textarea>`、以及 input/keyup/focus 等其它事件。
文本类内容请在业务按钮的回调里一次性读取，不要指望每个输入框都有回调。

### 三、以下按钮会被**故意跳过**，不要给它们加 `data-jade-handler`

这些是界面管道，不是业务逻辑。插件按四条规则过滤：

1. 位于 `.window-controls` 里的最小化/关闭按钮；
2. class 命中 `close` / `dismiss` / `minimize` / `maximize` 一类，例如
   `close-modal`、`close-btn`、`modal-close`、`browser-card-close`；
3. id 或内联 `onclick` 的函数名形如 `closeQrModal()`、`hideBrowserChoice()`、
   `dismissDialog()`（即 close/hide/dismiss/minimize/maximize 后面跟着
   window/modal/dialog/browser/qr）；
4. **弹窗消失型按钮**：按下时它所在的 `.modal` / `[role=dialog]` / `<dialog>` 还看得见，
   处理完就不见了，而且这次点击既没有调用 `jade.invoke(...)`，也没写任何
   `data-jade-*`。典型就是全站共用那个提示框的"确定"、确认框的"取消"。

第 4 条尤其重要：一个提示框会被十几种流程复用，如果给它生成回调，
就会出现十几个不同业务共用一个子程序名的情况，而一个 DOM 控件只能对应一个子程序名。

**注意这是个双向开关**：只要写了 `data-jade-handler`、`data-jade-channel`、
`data-jade-event` 或 `data-jade-call` 中任意一个，第 4 条就不再生效，这个按钮
会重新生成代码。所以"确定/取消"如果真的要干活（比如确认删除、确认提交），
就大方地加上 `data-jade-handler`；如果只是关窗口，就一个属性都别加。

被跳过时插件会在日志里写一行
`UI_EVENT control_ignored control="btnInfoOk" dom=click reason=dialog_dismiss`，
所以不会静悄悄地丢掉，可以查。

### 四、通道（订阅名）怎么定

易语言那边是 `JadeView.通讯.订阅（"通道名"）`。通道按这个优先级取：

1. `data-jade-channel="app:import_accounts"`（写死，优先级最高，会盖掉下面全部）
2. `data-jade-event="..."`（同义，优先级次之）
3. 这次点击过程中页面自己调用的 `jade.invoke('app:xxx')` —— **推荐走这条**
4. 都没有时退化成 `ui:<id>`

**推荐做法**：业务按钮在 JS 里正常写 `jade.invoke('app:xxx', 参数)`，通道就自动是
`app:xxx`，语义清楚，也不用多写属性。只有纯前端的按钮（比如只是弹出一个弹窗）
才会落到 `ui:<id>`。

`jade.invoke(...)` 必须在点击处理函数里**同步**调到（`await` 之前），
异步或 `setTimeout` 里再调就来不及被识别。

### 五、可选属性

| 属性 | 作用 | 默认 |
|---|---|---|
| `data-jade-handler` | 完整子程序名（**主要用这个**） | `<名字>_<事件后缀>` |
| `data-jade-name` | 可选的界面显示名称；不用于映射易语言变量，也不会替代稳定控件 ID | `id` |
| `data-jade-channel` | 写死订阅通道 | 见上文优先级 |
| `data-jade-assembly` | 指定生成到哪个程序集 | `Jade_通讯_订阅集` |

一般只需要 `data-jade-handler` 一个。列表对象绑定由易语言中的 `Jade超级列表框绑定` 赋值负责，`data-jade-name` 不用于把 HTML 控件映射到易语言变量，也不应为了事件生成而添加。`data-jade-name` 会改变通道，除非你同时
写死了 `data-jade-channel`，否则**不要用**。

### 六、中文名字自己的规矩

- 直接用按钮上那句可见文字，去掉图标和空格：`导入账号`、`一键连接`、`获取二维码`。
- 名字要在整个工程里唯一。多处出现"删除"就加上下文：`删除账号_被单击`、
  `删除任务_被单击`，不要两个都叫 `删除_被单击`。
- 复选框写它筛掉/开启的东西，别写选项字面值：`排除男性_选中状态被改变`
  比 `男_选中状态被改变` 清楚。
- **不要用空格、短横线、括号、点号**。所有 ASCII 标点都会被替换成下划线，
  `确认删除(账号)` 会变成 `确认删除_账号`。中文标点也别用。
- 数字开头会被自动补一个下划线；名字超过 120 字符会被截断。
- 中文、字母、数字、下划线是安全的，中文原样保留。

### 七、完整示例

```html
<!-- 业务按钮：中文名 + 英文 id，JS 里正常 invoke -->
<div class="page-actions">
  <button class="btn" id="btn-import-accounts" data-jade-handler="导入账号_被单击">
    <i class="fa fa-upload"></i> 导入账号
  </button>
  <button class="btn" id="btn-connect-all" data-jade-handler="一键连接_被单击">
    <i class="fa fa-plug"></i> 一键连接
  </button>
</div>

<!-- 复选框 -->
<label><input type="checkbox" id="filter-male" data-jade-handler="排除男性_选中状态被改变">男</label>

<!-- 下拉框 -->
<select id="task-mode" data-jade-handler="任务模式_选择项被改变">
  <option value="fast">快速</option>
  <option value="safe">稳妥</option>
</select>

<!-- 弹窗：干活的加属性，关窗口的不加 -->
<div class="modal" id="remove-modal">
  <div class="modal-content">
    <div class="modal-header">
      <h3>删除账号</h3>
      <!-- 窗口管道，class 已被识别，不加属性 -->
      <button class="close-modal" onclick="closeRemoveModal()"><i class="fa fa-times"></i></button>
    </div>
    <div class="modal-body">
      <!-- 只关窗口，不加属性，插件会自动跳过 -->
      <button id="btnRemoveCancel">取消</button>
      <!-- 真的干活，加属性 -->
      <button id="btnRemoveConfirm" data-jade-handler="确认删除账号_被单击">确认删除</button>
    </div>
  </div>
</div>

<script>
document.getElementById('btn-import-accounts')
  .addEventListener('click', () => jade.invoke('app:import_accounts'));
document.getElementById('btnRemoveConfirm')
  .addEventListener('click', () => jade.invoke('app:remove_account', JSON.stringify({ uid })));
</script>
```

上面这段在易语言里点一遍，会得到 `导入账号_被单击`、`一键连接_被单击`、
`排除男性_选中状态被改变`、`任务模式_选择项被改变`、`确认删除账号_被单击` 五个子程序，
"取消"和右上角关闭不会生成任何东西。

### 八、如果是改造我已有的 HTML

不要重写页面，只做加法：

1. **不要改动任何 `id`、`class`、`name`、内联 `onclick`，也不要动 JS。**
   现有 CSS 选择器和 `getElementById` 全靠它们，改了就是白改。
2. 从头到尾找出所有 `<button>`、`[role=button]`、`<input type=button|submit>`、
   `<input type=checkbox|radio>`、`<select>`。其它标签一律不用管。
3. 逐个判断：是第三节里那四类管道按钮吗？是就跳过，一个属性都不加。
4. 剩下的业务控件，按可见文字起中文名，加上 `data-jade-handler`，
   属性紧跟在 `id` 后面，别打乱原有属性顺序。
5. 最后给我一张表：控件 id、新的中文子程序名、以及被你跳过的控件和跳过的理由。
   跳过的那些我要复核。

### 九、交付前自检

- [ ] 每个业务按钮/复选框/单选框/下拉框都有 `data-jade-handler`，且后缀正确
- [ ] 所有 `id` 仍是英文 kebab-case，没有被改成中文
- [ ] 中文名全工程唯一，没有空格和标点
- [ ] 纯关闭/取消类按钮**没有**任何 `data-jade-*` 属性
- [ ] 干活的"确定/确认"按钮**有** `data-jade-handler`
- [ ] 业务按钮在 JS 里同步调用了 `jade.invoke('app:xxx')`

====

## 给自己看的备注（不用粘给 AI）

- 规则实现在 [src/WebPreview.cpp](../src/WebPreview.cpp) 的 `InstallUiEventBridge`：
  `stableName` / `isWindowControl` / `dismissedDialog` / `emit`。
- 通道退化成 `ui:<id>` 在 [src/IdeEventRouter.cpp:3077](../src/IdeEventRouter.cpp#L3077)。
- 名字清洗在 [src/IdeEventRouter.cpp:224](../src/IdeEventRouter.cpp#L224) 的 `SanitizeIdentifier`，
  大于 0x80 的字符原样保留，所以中文安全。
- 运行日志在 **当前工程目录\JadeDesigner.log**，每次运行只有一个文件。
- 改了 HTML 不用重启易语言，插件 600 ms 轮询会自动热重载。
- 改名之后，易语言工程里按旧英文名生成的子程序会变成孤儿，需要手工删掉。
