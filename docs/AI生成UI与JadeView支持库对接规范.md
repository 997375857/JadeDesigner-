# AI 生成 UI 与 JadeView 易语言支持库对接规范

版本：1.0  
适用范围：JadeView 网页 UI、JadeDesigner 可视化设计器、JadeView 易语言支持库  
核对依据：当前 JadeView 模块源码与 JadeDesigner 工程源码，核对日期 2026-09-15

## 给 AI 先看的硬性要求

你生成的不是普通浏览器网页，而是**必须适配易语言 JadeView 支持库和 JadeDesigner 可视化编辑的 UI**。除非用户明确说不需要易语言联动，否则必须遵守下面的要求：

1. 所有需要易语言操作的控件都要有唯一稳定的 `id`，并且设置相同的 `data-jade-id`。
2. 页面必须有 JavaScript 适配层，真正监听 `jade:data-control:update` 并按 `controlId`、`action`、`payload` 更新控件；只写 `data-*` 属性不算完成。
3. 需要易语言处理的业务事件必须同时有中文 `data-jade-handler`、明确的 `data-jade-channel` 和实际的 `jade.invoke` 调用。
4. 超级列表框必须采用固定行高的虚拟滚动：滚动容器 + 顶部占位 + 可视行 + 底部占位。数据量大时 DOM 只保留可视行和少量缓冲行，禁止把全部数据都渲染成 DOM。
5. 超级列表框的插入、删除、修改和批量更新只能更新数据数组与受影响的可视行，禁止每次操作重建整个列表或整页 `innerHTML`。
6. 必须实现 `beginBatch` / `endBatch` 批量边界，在批量结束时统一渲染；滚动刷新使用 `requestAnimationFrame` 合并，滚动监听使用 `passive: true`。
7. 不要伪造 `window.jade`，不要重复 JSON 编码，不要私自在页面初始化时添加测试数据；易语言是数据源时等待易语言下发数据。
8. 最终必须说明：控件对照表、绑定代码、事件通道、页面适配动作、未接入事件，以及浏览器测试和易语言真实运行测试的区别。

**超级列表框的性能实现顺序：固定行高虚拟滚动 > 状态与 DOM 分离 > 批量合并渲染 > 行节点复用 > 事件委托。** `will-change` 只是辅助提示，不能代替虚拟滚动，也不能给每一行都设置。

## 这份文档解决什么问题

### Jade设计草稿交付给 AI 时

Jade设计与 Jade预览是不同阶段：前者保存用户布局和控件需求，后者展示实际工程网页。收到 `工程.e.jade.design.json` 或 `工程.e.jade-ai-task.md` 时，AI 必须先阅读已有 `web` 源码，按设计意图增量实现本规范要求的运行适配层，不能把草稿控件当成已经实现的网页组件。

- 稳定 ID、已有通道和易语言绑定必须保留；未提及的控件和业务逻辑不能删除。
- 设计稿中超级列表框的列数量、标题、列宽和行高是明确配置；默认占位列或从现有网页导入后未核对的列，必须先与用户确认。
- 画布坐标是布局意图，正式页面优先用 Grid/Flex 响应式布局；不得照搬为整页固定绝对定位。
- “导出任务”“保存设计稿”不等于“HTML 已实现”或“易语言联调通过”。不得伪造验证状态。
- 现有网页的控件身份导入不保证还原布局，也不代表已有网页满足全部动作协议；仍需检查下文全部契约。

这不是普通的 HTML 美化提示词，而是给 AI 生成 UI 时使用的**联动契约**。目标是：

- AI 生成的 HTML 能被 JadeDesigner 识别、选中和编辑。
- 易语言可以按照接近原生控件的习惯绑定超级列表框、编辑框、下拉框等控件。
- 网页按钮、列表事件和易语言回调之间的通道、参数、返回值保持一致。
- 页面重新加载、多个控件、多个 JadeView 窗口同时存在时，不互相串数据。
- 明确哪些功能由支持库负责，哪些功能必须由页面自己的 JavaScript 负责。

## 最重要的结论

**仅仅给元素加 `data-jade-id`、`data-jade-control` 或 `data-jade-channel`，不会自动让它和易语言联动。**

一个可用的控件至少需要三层：

1. HTML 中有稳定的控件身份和可编辑结构。
2. 页面 JavaScript 有这个控件的状态、渲染和动作适配代码。
3. 易语言支持库绑定同一个控件 ID，并通过 JadeView 通讯发送或接收约定的数据。

如果只完成第 1 层，页面只能被设计器识别，不能保证“易语言插入一行后网页出现一行”。

本文中的“模块已支持”表示当前易语言模块已经提供相应公开接口；“页面必须实现”表示 AI 生成的页面必须自己实现对应的 DOM、状态和动作处理；“暂未支持”表示不要在生成代码中假装已经存在。

---

## 一、给 AI 的总提示词

生成 JadeView UI 时，把下面的规则作为系统要求或项目要求使用：

```text
你正在为 JadeView + JadeDesigner + 易语言支持库生成网页 UI。

1. 所有需要易语言操作的控件必须有全页唯一且稳定的 id，并同时写相同的 data-jade-id。
2. data-jade-control 只表示控件类型和适配目标，不代表运行时自动拥有功能；必须在页面 JavaScript 中实现状态、渲染和 jade:data-control:update 动作处理。
3. 所有需要易语言处理的业务事件必须写 data-jade-handler 和 data-jade-channel，并在 JavaScript 中对同一个精确通道调用 jade.invoke。属性不能代替实际调用。
4. 不要伪造 window.jade，不要覆盖真实 JadeView bridge，不要把 JSON 重复编码成带外层引号和反斜杠的字符串。
5. 绑定控件的 id 和 data-jade-id 必须一致，因为 JadeView.网页.取控件值当前按 DOM id 查找。
6. 页面状态必须按控件 ID 分开保存，不能把多个列表、多个下拉框或多个窗口共用一个数组。
7. 易语言是数据源时，不要在页面初始化时私自添加测试行；页面应等待易语言下发 setRows、append、insert 等动作。
8. 超级列表框必须保留固定行高、顶部占位、底部占位和可视区域渲染；插入、删除、修改后只更新受影响的可视 DOM，不要重建整页。
9. 事件 JSON 字段名、动作名和参数名必须按本文档，不要自行改成另一套协议。
10. 当前模块没有实现的事件（例如编辑框输入改变、复选框选中状态改变）不要生成易语言回调，也不要在说明中声称已经支持。
11. 输出 HTML、CSS、JavaScript 后，附上控件对照表、易语言绑定代码、事件通道表、未接入功能和真实测试步骤。
```

这段提示词只能约束 AI 生成代码，不能替代页面适配器。最终页面仍要检查 JavaScript 是否真的注册了动作和事件。

---

## 二、控件身份和 HTML 写法

### 2.1 稳定 ID 是绑定主键

需要易语言绑定的每个控件都必须满足：

```html
<div
  id="用户列表"
  data-jade-id="用户列表"
  data-jade-control="super-list"
></div>
```

要求：

- `id` 在整个页面唯一。
- `data-jade-id` 与 `id` 完全相同。
- 控件重命名时同步修改易语言绑定代码、页面脚本注册表和事件通道。
- 多个 JadeView 窗口可以使用相同的控件 ID，但绑定时必须传入正确的窗口 ID；同一个窗口内不能重复。
- ID 是机器通信标识，不要因为显示文字改变而改变 ID。
- 中文 ID 可以使用，但必须在 HTML、JavaScript 和易语言三处完全一致。

### 2.2 `data-jade-control` 的建议值

以下值用于让 AI、设计器和页面适配器理解控件类型：

| 类型 | `data-jade-control` 建议值 | 推荐 HTML 根元素 |
| --- | --- | --- |
| 超级列表框 | `super-list` | `div` |
| 编辑框 | `edit` | `input` 或 `textarea` |
| 下拉框 | `select` | `select` |
| 进度条 | `progress` | `progress` 或自定义进度容器 |
| 滑块 | `slider` | `input type="range"` |
| 数值框 | `number` | `input type="number"` |
| 树形框 | `tree` | `div` |
| 复选框 | `checkbox` | `input type="checkbox"` |
| 单选框 | `radio` | `input type="radio"` |
| 选项卡 | `tabs` | `div`，内部使用 `role="tablist"` |

这些属性是项目约定和适配器的识别信息。不能据此推断 JadeView 会替页面自动补齐渲染逻辑。

### 2.3 可视化编辑友好的文案

- 可编辑标题保留为 HTML 静态文字节点。
- 推荐给标题节点加 `data-jade-label`，例如 `<span data-jade-label>启用功能</span>`。
- 图标和文字分成两个节点，不要把重要文字写进 CSS `content`。
- 不要用整页 `innerHTML` 重建来更新一行数据，否则设计器选中的 DOM、事件和焦点都容易丢失。
- 不要把整页做成 Canvas、图片或 Shadow DOM，除非明确接受无法按普通 HTML 控件编辑的代价。

---

## 三、页面 JavaScript 必须有运行时适配层

### 3.1 适配层的职责

每个页面都需要一层明确的控件适配代码，负责：

- 按 `id` 注册控件状态。
- 监听 `jade:data-control:update`。
- 校验 `controlId`、`action` 和 payload。
- 把动作应用到对应控件的状态和 DOM。
- 只更新受影响的行、选项、节点或页签。
- 在网页事件发生时，按约定通道调用 `jade.invoke`。
- JadeView bridge 不存在或调用失败时保持页面可用，并在开发日志中说明原因。

### 3.2 动作信封

易语言数据控件对象发送的动作最终是下面这种信封：

```json
{
  "controlId": "用户列表",
  "action": "insert",
  "requestId": "jade-1001-1",
  "payload": {
    "index": 0,
    "row": {
      "title": "漂流瓶",
      "columns": ["漂流瓶"],
      "imageIndex": -1,
      "stateImageIndex": -1,
      "indent": 0,
      "value": 0
    }
  }
}
```

实际代码必须按 JSON 对象处理：

- 收到字符串时只解析一次。
- 不使用 `eval`。
- 不根据用户输入拼接可执行代码。
- 不把整个信封再次 `JSON.stringify` 后作为 payload 字符串发送。
- `requestId` 用于日志关联，不代表网页已经成功应用。
- 当前模块的 `requestId` 是对象内序号，不保证全局唯一；日志关联至少使用“窗口 ID + 控件 ID + requestId”。

### 3.3 适配器骨架

下面是结构示例。`renderList`、`renderTree` 等函数必须由页面根据自身 DOM 实现，不能直接照抄成假的通用功能：

```js
const controls = new Map();

function registerControl(element, type, state) {
  const id = element.id;
  if (!id || element.dataset.jadeId !== id) {
    throw new Error("Jade control requires matching id and data-jade-id");
  }
  controls.set(id, { element, type, state });
}

function parsePayload(payload) {
  if (payload == null || payload === "") return {};
  if (typeof payload === "string") return JSON.parse(payload);
  return payload;
}

function applyDataControlMessage(message) {
  const control = controls.get(message.controlId);
  if (!control || !message.action) return false;
  const payload = parsePayload(message.payload);

  switch (message.action) {
    case "setRows":
      control.state.rows = Array.isArray(payload.rows) ? payload.rows : [];
      renderList(control);
      return true;
    case "insert":
      return applyListInsert(control, payload);
    case "append":
      return applyListAppend(control, payload);
    case "setText":
      control.element.value = String(payload.value ?? "");
      return true;
    case "setPlaceholder":
      control.element.placeholder = String(payload.placeholder ?? "");
      return true;
    case "setSelectedIndex":
      control.element.selectedIndex = Number(payload.index);
      return true;
    default:
      return applyControlSpecificAction(control, message.action, payload);
  }
}

if (window.jade && typeof window.jade.on === "function") {
  window.jade.on("jade:data-control:update", (message) => {
    try {
      const applied = applyDataControlMessage(message);
      // 这里的日志只用于开发排查，不能当成易语言业务成功回执。
      console.debug("jade data-control", message.requestId, applied);
    } catch (error) {
      console.error("jade data-control apply failed", error);
    }
  });
}
```

这个骨架中的 `window.jade` 只读取真实 bridge，不得自行创建 `window.jade = {...}`。如果页面需要兼容 bridge 尚未加载的情况，应在 bridge 就绪后注册，或采用项目已有的就绪机制。

### 3.4 易语言启动顺序

推荐顺序：

```text
载入 JadeView / 创建网页窗口
设置 Jade_数据控件_设置默认窗口(窗口ID)
调用 Jade_数据控件_初始化()
绑定各个控件对象
再调用 设置数据、插入表项、加入选项等操作
```

窗口 ID 是 JadeView 网页窗口返回的窗口标识，不是 HTML 控件 ID，也不是 HWND。窗口 ID 可以省略，但省略时依赖默认窗口；为了多窗口和线程场景稳定，建议实际项目显式传入。

`页面版本` 当前保存于绑定对象，`检查页面版本()` 需要显式调用；当前动作信封不会自动携带页面版本，也没有自动重载握手。因此 AI 不得声称页面版本会自动阻止旧页面执行动作。

---

## 四、易语言公开绑定接口

所有绑定函数属于公开的数据控件模块。用户应直接保存返回的对象，后续用对象操作，不需要手工维护一个额外的“工厂类”。窗口 ID、页面版本可以省略；多窗口时建议填写。

| 公开绑定函数 | 返回对象 | 适用 HTML |
| --- | --- | --- |
| `Jade超级列表框绑定 (控件ID, [窗口ID], [页面版本])` | `JadeView超级列表框对象` | `data-jade-control="super-list"` |
| `Jade编辑框绑定 (控件ID, [窗口ID], [页面版本])` | `JadeView编辑框对象` | `input` / `textarea` |
| `Jade下拉框绑定 (控件ID, [窗口ID], [页面版本])` | `JadeView下拉框对象` | `select` |
| `Jade进度条绑定 (控件ID, [窗口ID], [页面版本])` | `JadeView进度条对象` | `progress` |
| `Jade滑块绑定 (控件ID, [窗口ID], [页面版本])` | `JadeView滑块对象` | `input type="range"` |
| `Jade数值框绑定 (控件ID, [窗口ID], [页面版本])` | `JadeView数值框对象` | `input type="number"` |
| `Jade树形框绑定 (控件ID, [窗口ID], [页面版本])` | `JadeView树形框对象` | `data-jade-control="tree"` |
| `Jade复选框绑定 (控件ID, [窗口ID], [页面版本])` | `JadeView复选框对象` | `input type="checkbox"` |
| `Jade单选框绑定 (控件ID, [窗口ID], [页面版本])` | `JadeView单选框对象` | `input type="radio"` |
| `Jade选项卡绑定 (控件ID, [窗口ID], [页面版本], [页面标题JSON])` | `JadeView选项卡对象` | `role="tablist"` |

示例：

```text
.版本 2

全局_用户列表 ＝ Jade超级列表框绑定 (“用户列表”)
全局_搜索框 ＝ Jade编辑框绑定 (“搜索框”)
全局_任务模式 ＝ Jade下拉框绑定 (“任务模式”)
全局_组织树 ＝ Jade树形框绑定 (“组织树”)
```

绑定成功只表示对象已经准备好；网页是否存在、动作是否被页面适配器应用，需要通过页面实际运行和日志验证。

---

## 五、控件 HTML 最小要求和公开能力

### 5.1 编辑框

```html
<input id="搜索框" data-jade-id="搜索框" data-jade-control="edit" type="text">
```

模块已支持：

| 易语言调用 | 页面动作 | payload |
| --- | --- | --- |
| `置文本 (文本)` | `setText` | `{value:string}` |
| `置提示文本 (提示文本)` | `setPlaceholder` | `{placeholder:string}` |
| `获取焦点 ()` | `focus` | `{}` |
| `失去焦点 ()` | `blur` | `{}` |

当前暂不生成易语言事件：光标位置、选中文本、内容改变、回车、获得焦点、失去焦点。页面可以有本地交互，但不能把它误报成已接入的支持库事件。

读取控件值使用公共的异步接口 `JadeView.网页.取控件值`，不是编辑框对象上的同步文本 getter。当前实现按 `document.getElementById` 查找，因此必须保持 `id` 与 `data-jade-id` 一致；返回结果通过网页 JavaScript 结果事件异步到达。

### 5.2 下拉框

```html
<select id="任务模式" data-jade-id="任务模式" data-jade-control="select"></select>
```

模块已支持：

| 易语言调用 | 页面动作 | payload |
| --- | --- | --- |
| `清空 ()` | `clearOptions` | `{}` |
| `加入选项 (标题, 数值)` | `addOption` | `{label:string,value:int}` |
| `置当前选项 (索引)` | `setSelectedIndex` | `{index:int}` |
| `置选项文本 (索引, 标题)` | `setOptionText` | `{index:int,label:string}` |
| `置选项数值 (索引, 数值)` | `setOptionValue` | `{index:int,value:int}` |

本地缓存查询：`取选项文本`、`取选项数值`、`取当前选项`、`查找选项`。选项索引从 0 开始；查找默认从 0 开始并精确匹配，指定假时按标题首部匹配；未找到返回 -1。因为当前查询读取模块缓存，页面单方面改变 `<select>` 后，易语言缓存不会自动同步。

下拉框选项值必须是整数。如果页面业务需要字符串主键，应在页面另外保存字符串 ID 映射，不要把字符串直接塞到当前模块的整数参数中。

### 5.3 进度条、滑块、数值框

```html
<progress id="任务进度" data-jade-id="任务进度" data-jade-control="progress" min="0" max="100" value="0"></progress>
<input id="任务滑块" data-jade-id="任务滑块" data-jade-control="slider" type="range" min="0" max="100" value="0">
<input id="重试次数" data-jade-id="重试次数" data-jade-control="number" type="number" min="0" max="10" step="1" value="0">
```

进度条和滑块支持范围、位置；数值框支持范围、步长、数值。进度条原生元素没有 `min` 的标准语义，页面若使用非零最小值，应由适配器把逻辑值换算为显示比例，不能直接假设 `<progress value>` 就等于模块数值。

当前滑块没有公开的 `置步长` 接口；数值框有 `置步长`。三个控件的 `取位置` / `取数值` 是最近一次由易语言设置的缓存值，不是自动从网页实时读取的同步 getter。

### 5.4 复选框和单选框

```html
<label>
  <input id="启用功能" data-jade-id="启用功能" data-jade-control="checkbox" type="checkbox">
  <span data-jade-label>启用功能</span>
</label>

<label>
  <input id="模式普通" data-jade-id="模式普通" data-jade-control="radio" type="radio" name="模式">
  <span data-jade-label>普通</span>
</label>
```

模块已支持标题和选中状态的主动设置、缓存读取。当前用户明确暂不做选中状态改变事件，因此不要为这两个独立控件生成易语言状态改变回调。

单选框必须使用正确的 HTML `name` 分组。易语言对象缓存不能自动知道浏览器因为同组互斥而取消了另一个单选框；如果业务要求实时同步，应等待后续专门的状态事件支持。

### 5.5 选项卡

```html
<div id="主选项卡" data-jade-id="主选项卡" data-jade-control="tabs" role="tablist">
  <button type="button" role="tab" data-tab-index="0" aria-selected="true">概览</button>
  <button type="button" role="tab" data-tab-index="1" aria-selected="false">设置</button>
</div>
<section data-tab-panel="0">概览内容</section>
<section data-tab-panel="1" hidden>设置内容</section>
```

模块支持添加、删除、清空、改标题、查找、设置当前页。**页签按钮和内容面板的显示切换必须由页面适配器实现**；当前模块动作只负责对页签控件下发 `setTabIndex` 等动作，不会自动找到任意 HTML 面板。

删除或插入页签时，页面必须同步更新 `data-tab-index`、`aria-selected`、`hidden` 和当前索引，不能只改变按钮文字。

---

## 六、超级列表框对接规范

超级列表框是本项目优先建设的核心控件，也是最容易因为 HTML 结构不一致而失效的控件。

### 6.1 根元素和行结构

```html
<div
  id="用户列表"
  data-jade-id="用户列表"
  data-jade-control="super-list"
  role="grid"
  aria-rowcount="0"
>
  <div class="list-header" role="row">
    <div class="list-header-cell" role="columnheader" data-column-index="0">用户名</div>
    <div class="list-header-cell" role="columnheader" data-column-index="1">状态</div>
  </div>
  <div class="list-spacer list-spacer-start" aria-hidden="true"></div>
  <div class="list-visible-rows"></div>
  <div class="list-spacer list-spacer-end" aria-hidden="true"></div>
</div>
```

推荐每一行使用：

```html
<div class="list-row" role="row" data-row-index="0" data-row-id="用户-1">
  <div class="list-cell" role="gridcell" data-column-index="0">张三</div>
</div>
```

要求：

- 行索引从 0 开始。
- 行数据保存在 JavaScript 数组或状态对象中，DOM 只保留可视行。
- `data-row-id` 是业务稳定键；索引会因插入、删除而变化，不能当永久业务 ID。
- 每个单元格写 `data-column-index`，事件不得把所有列硬编码成 0。
- 必须有表头，表头单元格使用 `role="columnheader"`，并与数据单元格使用相同的 `data-column-index`。
- 标题、图片、状态图片、缩进、表项数值、选择框和单选框都应属于该行状态。

### 6.1.1 AI 生成前必须确认表头和列

在 HTML 语义上，易语言“超级列表框”对应 **Virtualized Data Grid（虚拟化数据网格）**，不能只生成一个没有表头的普通列表。生成 UI 前，AI 必须先向用户确认：

- 需要多少列。
- 每一列的标题文字。
- 每一列的用途或数据类型（文本、时间、状态、图片、选择框等）。
- 是否需要排序、固定列宽、行选择或行内操作。

通常情况下，**列是初始化时固定配置的表头结构，行是运行时由易语言动态插入、修改和删除的数据**。列定义和行数据要分开维护，不能把每一行的列结构重复写进 HTML。

建议 AI 先用下面的方式确认需求：

```text
这是一个易语言超级列表框，对应 HTML 的 Virtualized Data Grid。
请先告诉我：需要几列？请按“列序号：列标题”逐列列出，例如：
0：用户名
1：状态
2：最后发言时间
如果某列需要图片、选择框、单选框、排序或固定宽度，也请一并说明。
```

确认后，AI 才生成带表头的 `role="grid"` 结构，并为每个单元格设置正确的 `data-column-index`。不得擅自猜测列数、列标题或把所有数据塞进单列。

### 6.2 易语言常用写法

```text
索引 ＝ 全局_用户列表.插入表项 (, “漂流瓶”, , , , )
全局_用户列表.置标题 (索引, 1, “内容文本”)
全局_用户列表.置图片 (索引, 图片索引)
```

批量更新时：

```text
全局_用户列表.禁止重画 ()
全局_用户列表.插入表项 (, “第一行”, , , , )
全局_用户列表.插入表项 (, “第二行”, , , , )
全局_用户列表.置标题 (0, 1, “内容文本”)
全局_用户列表.允许重画 ()
```

`禁止重画` / `允许重画` 是成对使用的批量边界，支持嵌套。页面适配器收到 `beginBatch` 后应延迟重复渲染，收到最后一个 `endBatch` 后再统一刷新；不能因为每个动作都立即重建整个列表而失去性能。

#### 插入表项的默认参数

AI 生成易语言代码时，必须遵守易语言超级列表框的默认行为：

| 插入位置参数 | 实际行为 |
| --- | --- |
| 省略 | 插入列表尾部 |
| `-1` | 插入列表尾部 |
| `0` 或更大整数 | 插入指定索引；超过当前行数时按尾部处理 |

因此，页面适配器收到 `insert` 动作时，`index` 缺省或为 `-1` 都必须转换为当前行数，不能把缺省值按 `0` 插入第一行。推荐的易语言写法如下：

```text
索引 ＝ 全局_用户列表.插入表项 (, “漂流瓶”, , , , )
.如果真 (索引 ＝ -1)
    返回 ()
.如果真结束
全局_用户列表.置标题 (索引, 1, “内容文本”)
```

对应的页面动作语义为：

```json
{"action":"insert","index":-1,"row":{"columns":["漂流瓶","内容文本"]}}
```

`插入表项` 的标题、图片、状态图片、缩进和表项数值属于行初始化参数；后续修改应分别使用 `置标题`、`置图片`、`置状态图片`、`置缩进数目` 和 `置表项数值`，不能把这些参数拼成一段需要页面自行猜测的 JSON 文本。

### 6.3 列表动作和行 JSON

模块公开动作对应关系：

| 易语言接口 | action | payload 主要字段 |
| --- | --- | --- |
| `设置数据 (数据JSON)` | `setRows` | `{rows:array}` |
| `添加 (行JSON)` | `append` | `{row:object}` |
| `插入表项 (位置, 标题, 图片, 状态图片, 缩进, 数值)` | `insert` | `{index:int,row:object}` |
| `修改 (键JSON, 行JSON)` | `update` | `{key:object,row:object}` |
| `删除 (键JSON)` | `remove` | `{key:object}` |
| `清空 ()` | `clear` | `{}` |
| `刷新 (选项JSON)` | `refresh` | 调用方提供的对象 |
| `置标题 (行, 列, 标题)` | `setTitle` | `{index:int,column:int,title:string}` |
| `置图片 (行, 图片索引)` | `setImage` | `{index:int,imageIndex:int}` |
| `置状态图片 (行, 状态图片索引)` | `setStateImage` | `{index:int,stateImageIndex:int}` |
| `置缩进数目 (行, 缩进)` | `setIndent` | `{index:int,indent:int}` |
| `置表项数值 (行, 数值)` | `setValue` | `{index:int,value:int}` |
| `删除表项 (行)` | `remove` | 当前实现使用数字键时为 `{key:number}` |
| `置列标题 (列, 标题)` | `setColumnTitle` | `{column:int,title:string}` |
| `置列宽 (列, 宽度)` | `setColumnWidth` | `{column:int,width:int}` |

插入表项生成的默认行至少包含：

```json
{
  "title": "漂流瓶",
  "columns": ["漂流瓶"],
  "imageIndex": -1,
  "stateImageIndex": -1,
  "indent": 0,
  "value": 0
}
```

页面适配器不得把空标题、空图片索引或 0 数值误判成“没有插入”。收到 `insert` 后必须先按上述默认规则规范化 `index`，再插入状态数组并重新计算可视区域。

### 6.4 选择框和单选框

```text
全局_用户列表.选择框置样式 (“默认”)
全局_用户列表.选择框宽高 (16, 16)
全局_用户列表.置选择框 (表项索引, 列索引, 2, “学过，常用”, 真)
全局_用户列表.置单选框 (表项索引, 列索引, 2, “普通，高级”, 真)
全局_用户列表.置选择框状态 (表项索引, 列索引, 0, 真)
全局_用户列表.置单选框状态 (表项索引, 列索引, 1, 真)
```

对应动作为 `setCheckboxStyle`、`setCheckboxSize`、`setCheckboxes`、`setRadios`、`setCheckboxChecked`、`setRadioChecked`。`说明文本` 是显示说明，`文字在前` 决定文字在控件前还是后。

当前模块缓存的选择框和单选框状态是**每一行一组**，不是任意多列都各自拥有独立组。因此页面可以支持一行中的多个选择框或一组单选框，但不要在未扩展模块前声称多个列中存在完全独立的同类组状态。

选择框样式名只是页面 CSS / 适配器约定。AI 生成页面时必须实际提供样式映射，例如：

```css
[data-jade-checkbox-style="默认"] .jade-check {
  width: 16px;
  height: 16px;
}

[data-jade-checkbox-style="紧凑"] .jade-check {
  width: 12px;
  height: 12px;
}
```

如果 CSS 没有对应样式，易语言发送成功也不会产生视觉变化。模块不会从任意 CSS 自动猜出样式名、宽度和高度。

### 6.5 图片索引

`置图片` 和 `置状态图片` 发送的是整数索引，不是图片 URL。页面必须提供自己的索引到资源 URL 映射，并使用相对路径加载资源。未定义索引时应显示无图或默认图，不能让错误字符串进入 `src`。

### 6.6 超级列表框虚拟滚动

推荐保持：

1. 一个滚动容器。
2. 顶部占位高度 = `start * rowHeight`。
3. 中间只渲染可视行和少量缓冲行。
4. 底部占位高度 = 剩余行数 * `rowHeight`。
5. `requestAnimationFrame` 合并滚动刷新，滚动监听使用 `passive: true`。
6. 行容器使用 `contain` 隔离布局；只有确实测量过需要时才使用 `will-change`。

### 6.7 批量数据的进一步优化

对于直播弹幕、日志、消息流或一次性导入大量数据，建议 AI 按下面的方式实现：

- **动作队列合并**：同一批动作先进入页面端队列，在一次 `requestAnimationFrame` 中处理；同一行同一列连续多次 `setTitle` 只保留最后一次，但 `insert`、`remove` 必须按原顺序执行。
- **行节点复用**：可视窗口滚动时优先复用现有行节点，只替换文本、图片、状态和 `data-row-index`，避免不停创建和销毁 DOM。
- **事件委托**：列表根元素只绑定一次 click、dblclick、contextmenu 和指针事件，通过最近的 `[role="row"]` / `[role="gridcell"]` 找出行列，不给一万行分别绑定监听。
- **单次 JSON 解析**：一个动作只解析一次信封和 payload；不要在每个单元格渲染函数里再次解析同一行 JSON。
- **增量索引**：如果业务经常按稳定键修改或删除，维护 `rowId -> 行位置` 映射；插入和删除后只修正受影响区间，不要每次全表线性扫描。
- **超长列表上限**：虚拟滚动解决 DOM 数量，不会自动解决无限增长的内存。消息流应提供最大缓存条数、淘汰旧行或分页策略，并把淘汰规则交给业务确认。
- **测量一次**：用实际 CSS 计算固定 `rowHeight`，窗口大小变化时通过 `ResizeObserver` 重新计算；不要在每个滚动事件中强制读取布局再写布局。
- **慎用 GPU 提示**：`will-change: transform` 只给正在移动的可视层使用，不能给所有行使用；过度使用会增加显存和合成层，反而变慢。

不建议默认使用 `content-visibility: auto`、每行独立 `will-change` 或全量 `innerHTML`。这些属性可能改变测量、焦点和可视化编辑行为；只有完成实际页面测试后才可局部采用。

插入行不会破坏固定行高：只调整状态数组、总高度和受影响的可视行。不要因为插入一行就把一万行全部变成 DOM，也不要通过整页 `innerHTML` 重建来刷新。

---

## 七、树形框对接规范

### 7.1 HTML 要求

```html
<div id="组织树" data-jade-id="组织树" data-jade-control="tree" role="tree"></div>
```

树节点必须使用稳定节点 ID，而不是 DOM 顺序作为唯一身份。节点至少包含：

```json
{
  "id": 1,
  "parentId": -1,
  "label": "总公司",
  "value": 0,
  "expanded": true,
  "selected": false
}
```

### 7.2 易语言接口

| 易语言接口 | 说明 |
| --- | --- |
| `添加主节点 (标题, [节点值])` | 根节点，返回稳定节点 ID |
| `添加子节点 (父节点, 标题, [节点值])` | 支持任意层级，父节点不存在返回 -1 |
| `删除节点 (节点, [是否删除子节点])` | 默认删除后代节点 |
| `清空 ()` | 清空所有节点 |
| `置标题 / 取标题` | 修改或读取节点标题 |
| `置节点值 / 取节点值` | 修改或读取整数值 |
| `展开节点 / 折叠节点` | 修改展开状态 |
| `置选中节点 / 取选中节点` | 修改或读取选中节点 |
| `展开全部 / 折叠全部` | 批量改变展开状态 |

页面必须处理任意层级、父子删除和折叠后的可视节点重排。树节点当前使用固定行高和可视缓存时，不能把折叠节点继续留在可视 DOM 中。

当前模块没有实现带权限的树形复选框、半选状态和父子级联。AI 可以生成这些纯网页效果，但必须标记为页面本地功能，不能生成不存在的易语言接口。

---

## 八、事件对接

### 8.1 普通业务事件

业务按钮需要同时具备中文回调名、明确通道和实际发送代码：

```html
<button
  id="保存配置"
  type="button"
  data-jade-id="保存配置"
  data-jade-handler="保存配置_被单击"
  data-jade-channel="app:save_config"
>
  保存配置
</button>
```

```js
function invokeBusiness(channel, payload) {
  if (!window.jade || typeof window.jade.invoke !== "function") {
    console.warn("JadeView bridge unavailable", channel);
    return Promise.reject(new Error("bridge-unavailable"));
  }
  return window.jade.invoke(channel, payload);
}

document.getElementById("保存配置").addEventListener("click", () => {
  invokeBusiness("app:save_config", { source: "保存配置" }).catch(console.error);
});
```

注意：`data-jade-handler` 和 `data-jade-channel` 是声明，`jade.invoke` 才是实际发送。每个操作只保留一套监听，不能同时使用内联 `onclick` 和重复的 `addEventListener`。

### 8.2 超级列表框事件

超级列表框公开事件名称和通道规则：

| 易语言事件名称 | 事件代码 | 通道 |
| --- | --- | --- |
| 被单击 / 单击 | `click` | `jade:list:event:控件ID:click` |
| 被双击 / 双击 | `dblclick` | `jade:list:event:控件ID:dblclick` |
| 选择框被点击 | `checkbox-change` | `jade:list:event:控件ID:checkbox-change` |
| 单选框被点击 | `radio-change` | `jade:list:event:控件ID:radio-change` |
| 表项被选中 | `select` | `jade:list:event:控件ID:select` |
| 右键单击 | `contextmenu` | `jade:list:event:控件ID:contextmenu` |
| 鼠标进入表项 | `mouseenter` | `jade:list:event:控件ID:mouseenter` |
| 鼠标离开表项 | `mouseleave` | `jade:list:event:控件ID:mouseleave` |
| 滚动到底部 | `scroll-bottom` | `jade:list:event:控件ID:scroll-bottom` |

易语言绑定示例：

```text
全局_用户列表.绑定事件 (“被单击”, &用户列表_被单击)
全局_用户列表.绑定事件 (“被双击”, &用户列表_被双击)
```

回调子程序签名：

```text
.子程序 用户列表_被单击, 整数型
.参数 窗口ID, 整数型
.参数 消息, 文本型

调试输出 (“用户列表单击”, 窗口ID, 消息)
返回 (JadeView.文本.创建指针 (“ok”))
```

普通事件 JSON 至少包含：

```json
{
  "controlId": "用户列表",
  "event": "click",
  "rowIndex": 0,
  "columnIndex": 1,
  "rowId": "用户-1",
  "row": {},
  "button": 0,
  "detail": 1,
  "clientX": 100,
  "clientY": 80,
  "ctrlKey": false,
  "shiftKey": false,
  "altKey": false
}
```

选择框事件额外包含 `checkboxIndex`、`checked`；单选框事件额外包含 `radioIndex`、`checked`；滚动到底部事件的 `rowIndex` 为 -1，并包含 `scrollTop`、`scrollHeight`。

事件中的 `row` 是事件发生时的行快照，不能只传一个固定的 `{}`。如果页面没有可用行 ID，至少保证行索引和列索引准确。

### 8.3 树形框事件

树形框事件通道为：

```text
jade:tree:event:控件ID:事件代码
```

当前事件代码为 `click`、`dblclick`、`select`、`expand`、`collapse`。JSON 至少包含 `controlId`、`event`、`nodeId`、`parentId`、`label`、`value`。页面事件必须针对真实节点发送，不能把整个树都发成一个固定节点。

### 8.4 暂不接入的事件

以下事件当前不能作为已完成的易语言支持库事件生成：

- 编辑框内容改变。
- 编辑框回车。
- 编辑框获得焦点、失去焦点回调。
- 光标位置变化、选中文本变化。
- 独立复选框的选中状态改变。
- 独立单选框的选中状态改变。
- 选项卡页切换回调。

页面可以保留本地 JavaScript 监听，但交付表中必须写“网页本地事件，未接入易语言回调”。

说明：超级列表框行内通过 `置选择框` / `置单选框` 创建的选择框和单选框不属于上述“独立控件”。它们已经属于超级列表框事件体系，支持 `checkbox-change` 和 `radio-change`，并按 8.2 节的列表事件通道回调易语言。

---

## 九、读取值、缓存和回执的边界

### 9.1 `JadeView.网页.取控件值`

这是公共异步接口，不是某个控件类上的同步 getter。它当前按 DOM `id` 查询，并通过 JavaScript 结果事件返回。对于 checkbox、radio，当前读取的是元素 `value`，不是 `checked` 状态；不要把它当成复选框实时状态读取接口。

### 9.2 类对象的“取”方法

下拉框的选项查询、列表的标题和选择状态、树节点信息等，主要读取易语言对象维护的缓存。网页单方面改变 DOM 不会自动改变这些缓存，除非该控件已经有对应的事件同步逻辑。

### 9.3 `逻辑型`返回值的含义

数据控件方法返回真，通常表示：

- 参数校验通过；
- 动作 JSON 生成成功；
- 动作已发送或排队。

它不等于网页已经找到元素、不等于 DOM 已完成渲染、不等于业务后台成功。当前回执只记录 `requestId`、控件 ID、动作和 `ok`，没有完整的待处理动作表、超时重试或事务回滚。AI 不得在 UI 提示中把“已发送”写成“服务器已保存”。

---

## 十、WPE、设计器和运行时边界

### 10.1 WPE 接管

JadeDesigner 只有在当前工程检测到 WPE 时，才接管对应的属性、事件和可视化区域；没有检测到 WPE 时，应明确提示“可能使用的是原生 UI，本次不接管”，不能把原生控件属性伪装成 JadeView 属性。

WPE 是设计器和支持库识别页面的入口条件，不是网页动作适配器。即使检测到 WPE，页面仍然必须具备本文所说的 HTML 和 JavaScript 适配层。

### 10.2 运行时文件

编译后的易语言程序需要能加载 JadeView 运行时 DLL，并能找到约定的 `web` 页面资源。设计器用的 FNE、钩子 DLL 或通信诊断窗口不等于编译后程序所需的运行时文件。

### 10.3 不要把诊断 UI 写进用户页面

通信诊断、请求 JSON、网页回执和耗时属于开发工具，不是业务页面功能。AI 生成的正式 UI 不应默认创建诊断弹窗，也不应依赖诊断窗口才能完成列表插入或按钮回调。

### 10.4 JadeDesigner 可视化接管和右键事件

JadeDesigner 的工具脚本会在 WPE 预览层统一注入，不需要每个易语言工程再手写一套“选中控件”或“右键菜单”代码：

- 只有带稳定 `data-jade-id`（或唯一 `id`）且类型可识别的控件才会被接管；没有稳定 ID 时只能查看，不能生成可靠的回调。
- 设计模式只选中控件；事件模式允许创建或定位易语言回调；预览模式只运行网页，不会因普通点击改写易语言代码。
- 在可识别的超级列表框、按钮、下拉框、复选框或单选框上右键，工具层显示模块实际支持的事件和控件 ID。事件菜单由 JadeDesigner 处理，不要求项目 HTML 增加右键监听。
- 双击原生“属性”页的可视化事件，或者在预览层右键选择“创建或跳转”，都会先检查现有回调：已存在则定位，不存在才创建，然后再定位到对应程序集和子程序。
- 事件列表只展示公开模块能力；初始化、销毁、缓存、基础对象等内部实现不应当作为用户可调用事件或控件成员暴露。

超级列表框的事件名和通道必须继续遵守 8.2 节；AI 生成页面时只需保证控件根节点有稳定 ID、正确的 `data-jade-control="super-list"`，以及实际动作所需的页面适配层。不要为 JadeDesigner 再复制一套项目级右键处理逻辑。

自动生成的易语言代码遵守以下规则：

1. 超级列表框对象由用户在普通程序集或全局变量中保存，例如 `全局_弹幕列表 ＝ Jade超级列表框绑定 ("danmaku-list", 全局_内部_主窗口ID, )`。设计器会从工程内存中找到这条已有绑定，不生成额外的 `Jade_列表_*` 程序集，也不生成重复对象变量。
2. 注册列表事件使用已有对象的 `对象.绑定事件 ("被单击", &回调)` 等模块命令，并把语句写入找到的绑定子程序；回调名沿用易语言对象习惯，以绑定变量名加事件名生成，例如 `全局_主播列表被单击`、`全局_主播列表被双击`。不把列表事件改写为直接的 `JadeView.通讯.订阅`，也不使用 HTML ID 哈希作为回调名。
3. 回调子程序统一补入 `Jade_通讯_订阅集`，绑定对象仍由用户原来的初始化路径调用。新增事件不会重置对象，也不会修改已有绑定赋值。若工程中没有对应的 `Jade超级列表框绑定`，设计器会提示先绑定，不会偷偷创建另一套列表对象。
4. 普通按钮、下拉框保留已有的 `data-jade-channel`；超级列表框事件通道按 8.2 节和事件类型生成，不复用根节点上某个普通业务通道。当前模块的通道不含窗口 ID：同 ID 跨窗口隔离尚不能由这套自动事件生成器保证，请使用不同控件 ID。
5. 对已有回调只定位或补缺失的绑定，不覆盖函数体。遇到旧式直接订阅、重复回调、重复控件 ID 或不匹配的回调签名时会提示冲突；应检查并合并已有代码，不能再叠加一份订阅。
6. 预览模式在网页和原生属性栏两侧均只读；页面刷新或项目切换后旧选择失效。列表的表头、空白区、内部行和单元格均定位到已声明的列表根节点。普通未声明的 HTML 表格不会自动当作已接入模块的超级列表框。

这些是**设计期的选中、生成与定位功能**，不是运行期的事件模拟器。页面适配层必须实际发送 8.2 节对应事件。不要因为预览右键能生成子程序，就把运行期事件上报省掉。独立复选框、单选框的状态改变事件仍未开放；普通按钮仅列出已接入的单击事件，不显示尚未接入的双击事件。

---

## 十一、性能和线程安全要求

### 11.1 页面端

- 每个控件 ID 独立保存状态。
- 列表使用虚拟滚动，固定行高必须和 CSS 实际行高一致。
- 滚动采用 `requestAnimationFrame` 合并更新和 passive listener。
- 不在每个动作中重建所有 DOM。
- 批量动作在 `beginBatch` / `endBatch` 之间合并渲染。
- 事件委托只绑定一次，避免每行重复绑定大量监听。
- JSON 解析失败、控件不存在、动作未知时记录明确错误，不要静默伪造成功。

### 11.2 易语言端

- 多线程调用同一个数据控件对象时必须遵守对象内部锁的边界，不要在外部绕过对象直接改缓存。
- 多窗口场景显式传窗口 ID，不依赖默认窗口推断。
- 回调中只处理必要的 JSON，耗时业务交给业务线程，避免阻塞 JadeView 通讯回调。
- `禁止重画` 与 `允许重画` 必须成对，即使中途某个动作失败也要保证最终恢复重画。
- 不要把回调返回的 `ok` 当成业务数据；业务结果应使用明确字段和单独通道返回。

---

## 十二、AI 交付前必须输出的内容

每次生成或改造 UI，AI 必须同时交付以下清单：

### 12.1 控件对照表

| 控件 ID | 类型 | 易语言绑定函数 | 中文回调 | 通道 | 页面适配动作 |
| --- | --- | --- | --- | --- | --- |
| 用户列表 | 超级列表框 | `Jade超级列表框绑定` | 被单击 | `jade:list:event:用户列表:click` | `setRows`、`insert`、`setTitle` |
| 搜索框 | 编辑框 | `Jade编辑框绑定` | 无 | 无 | `setText`、`setPlaceholder` |

### 12.2 不产生易语言回调的控件

说明是纯展示、本地交互、关闭提示框，还是当前模块尚未支持的事件。关闭按钮、取消按钮不应被误报成业务回调。

### 12.3 尚未接入的业务

列出“页面事件已经写好，但易语言尚未订阅或后台业务尚未实现”的项目，明确标注“未接入”。

### 12.4 验收步骤

至少验证：

1. 易语言绑定对象使用正确控件 ID 和窗口 ID。
2. 易语言 `插入表项` 后网页只增加对应一行，原有行顺序和固定行高不变。
3. `置标题`、`置图片`、选择框和单选框动作能命中正确行和列。
4. 多个列表分别操作时不串数据。
5. 列表滚动一万行时 DOM 数量保持在可视范围，不整页重建。
6. 点击、双击、选择框、单选框和滚动事件的 JSON 字段准确。
7. 页面刷新或 bridge 不可用时有清晰日志，不把本地模拟结果当成易语言真实结果。
8. 没有 WPE 的原生 UI 不被错误接管。

---

## 十三、常见错误对照

| 现象 | 常见原因 | 正确检查方向 |
| --- | --- | --- |
| 页面发送 `ui-test:list_add {}`，列表不增加 | 页面只是发了业务通道，没有易语言回调或回调没有调用列表对象 | 看易语言回调是否进入、参数是否为空、是否实际调用 `插入表项` |
| 易语言日志显示 `insert`，网页无变化 | 页面没有处理 `insert`，或 `controlId` 不匹配 | 检查适配器注册表、动作名、payload 是否为对象 |
| JSON 双击后有外层引号和反斜杠 | JSON 被重复编码 | 复制前取 JSON 文本本体，不要再次 `JSON.stringify` 字符串 |
| 列表行出现了，但标题列不对 | 页面只实现了 `title`，没有按 `column` 更新单元格 | 检查 `data-column-index` 和 `setTitle` 适配代码 |
| 选择框样式设置成功但外观不变 | 页面 CSS 没有对应样式名映射 | 检查 `setCheckboxStyle` 是否设置根节点属性和 CSS 是否匹配 |
| 插入一行后滚动位置或行高乱了 | 重新构建所有 DOM 或 CSS 行高不是固定值 | 保留虚拟滚动占位，更新数组和可视窗口 |
| 双击树节点后页签跳到程序页 | 设计器双击处理错误地切换了宿主选项卡 | 区分“定位源代码”和“保持通信诊断/属性页”的 UI 状态 |
| `取控件值` 读不到 | `id` 与 `data-jade-id` 不一致，或调用当成同步返回值 | 保持同 ID，并通过异步结果事件取值 |
| 多个窗口动作串到一起 | 省略窗口 ID并依赖错误的默认窗口 | 创建窗口后设置默认窗口，关键绑定显式传窗口 ID |
| 运行时弹出诊断窗口 | 把开发诊断代码编进正式页面或启动流程 | 诊断工具和正式业务页面分离 |

---

## 十四、最终原则

JadeView 的目标不是让用户学习一套新的网页框架，而是让 AI 生成的网页在易语言里具有接近原生控件的使用习惯。因此对外 API 应保持中文、对象化和按控件分类；对内网页应保持稳定 ID、明确动作、独立状态和局部渲染。

最小可靠闭环是：

```text
HTML 稳定控件
  -> 页面适配器注册控件和动作
  -> 易语言公开绑定函数取得对象
  -> 易语言对象生成动作 JSON
  -> JadeView 通讯发送
  -> 页面按 controlId/action 更新状态和 DOM
  -> 页面事件按准确 JSON 通道回调易语言
```

其中任何一环没有实现，都只能称为“页面标记存在”或“接口代码已写”，不能称为“UI 已经和易语言支持库联动完成”。
