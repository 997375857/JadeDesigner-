# e5.95 逆向记录：结果与收益（Reverse Notes）

日期：2026-08-28。目标：`e5.95.exe`（SHA256 `368CBBD323D2C5BC00F0C072B116333F75339FC0742E238D3E5E6FF17ABE1409`，
3,338,240 字节，PE32 x86，ImageBase `0x00400000`，重定位已剥离）。

本文回答两个问题：**逆向得到了什么**，以及**每个结果换来了什么**。
配套文档：`docs/e595_静态分析_2026-08-28.md`（菜单命令表、12 个内部文本包函数特征码）、`ARCHITECTURE.md`（插件架构）。

## 0. 方法论

1. **功能码常量扫描**：公开 IDE 功能码（`PublicIDEFunctions.h`）是确定的数值
   （如 `FN_SET_AND_COMPILE_PRG_ITEM_TEXT = 0x02020071`）。用 Python + capstone 在 `.text`
   里搜这些 4 字节常量并反汇编上下文——常量出现的位置就是"IDE 自己调用该功能"或"分发器比较该功能码"的地方。
2. **调用点上下文反汇编**：围绕命中地址反汇编 ±0x60 字节，还原参数压栈顺序与对象关系。
3. **行为对照实验**：在可丢弃工程里用插件实际调用，日志取证（反编译结论必须过实测这一关）。

复现脚本核心（完整版可在会话记录中找到）：

```python
import pefile, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE(r"%E_LANG_HOME%\e5.95.exe")
text = [s for s in pe.sections if s.Name.decode().rstrip('\x00') == '.text'][0]
data, text_va = text.get_data(), pe.OPTIONAL_HEADER.ImageBase + text.VirtualAddress
for name, code in {"FN_SET_AND_COMPILE_PRG_ITEM_TEXT": 0x02020071, ...}.items():
    # 在 data 中查找 struct.pack("<I", code) 并反汇编命中点前后指令
```

## 1. 反编译结果与收益对照

### 1.1 `FN_GET_PRG_TEXT` 处理器（VA `0x004C4D64`）

**结果：**
- 处理器从 `GET_PRG_TEXT_PARAM*`（寄存器 `[ebp]`）取行/列；**行为 -1 时使用编辑器对象当前光标**
  （对象 `+0x74` = 光标行，`+0x78` = 光标列）——证明该接口作用于"当前页"。
- 单元格文本经 `call 0x4B6400(this=编辑器, row, col, &文本指针, 0, 0)` 获取；
  字符串初始化调用 `0x4918B0`（即特征码表中的 `text_buffer_init`，与静态分析记录逐字节吻合）。
- 参数结构偏移实测：`[ebp+0x10]` 写 type、`[ebp+0x14]` 写 isTitle——与我们头文件的
  `GET_PRG_TEXT_PARAM` 布局完全一致。
- 尺寸分支（`m_pBuf==NULL`，VA `0x4C4E5B`）**有实现**：`repne scasb` 求长后
  `mov [ebp+0xC], ecx`，返回 `strlen+1`。
- **处理器结尾固定 `mov eax, 1`——对越界行也返回"成功"**，此时 type=0、BufSize=1、文本为空。

**收益：**
1. 确定读单元格的正确协议（两段式：NULL 查尺寸 → 给缓冲区读文本），文本读取从此可用——
   这是后来一切"按名称定位/按文本验证"的地基。
2. `永远返回 TRUE` 这一发现直接催生了 `CellHasData()` 证据规则（`type!=0 || isTitle || reportedSize>1 || 非空`）。
   1.6.7 日志里 `bottom_row=4095` 的假数据由此解释并修复；同时确立了全插件
   "公开接口返回值一律不可信，必须二次取证"的设计原则。
3. `type` 字段永远可靠（越界=0，真实单元格=VT 常量）→ 派生出"类型计数法"
   （`CountCellsOfType(VT_SUB_NAME)` 前后对比）作为不依赖文本回读的创建验证。

### 1.2 名称单元格提交路径（VA `0x0044D0B0`）

**结果：** IDE 自身处理名称单元格编辑提交时，生成如下调用：

```asm
mov  ecx, [esi+0x14C]      ; 编辑器宿主对象
mov  eax, [esp+0xC]        ; 新文本指针
push 0                     ; 参数4
push eax                   ; 参数3: 文本
push 0xF                   ; 参数2
push 0x2020071             ; 参数1: FN_SET_AND_COMPILE_PRG_ITEM_TEXT
call dword ptr [edx+0xE0]  ; 内部 RunFunc 分发
```

即 **IDE 自己改名就走 `FN_SET_AND_COMPILE_PRG_ITEM_TEXT`**；其后续调用
（`0x4B67F0` 光标/滚动、`0x4C2CC0` 刷新重编译、`0x44CD30` 重绘）构成完整的提交序列。

**收益：**
1. 一锤定音地回答了 0.9 版遗留问题——"公开接口到底能不能改名"。答案：能，
   且这就是 IDE 的原生路径，不存兼容性风险。
2. 结合头文件注释"设置**当前光标所处行列位置**的整体内容"，锁定失败根因：
   0.9 没先把光标移到名称单元格。修正后的 `FN_MOVE_CARET → SET_AND_COMPILE → 回读`
   流程一次通过（1.6.9 实测 `rename_cell set=1 verify=1 read_back="Jade注册事件"`，
   MDI 标题同步变为"程序集: Jade注册事件"）。
3. 子程序改名复用同一原语（`RenameCellAt`）：`FN_INSERT_NEW_SUB` 产生的"子程序N"
   被就地改名为目标事件名，省掉了任何文本替换。

### 1.3 编辑器键盘 `.` 输入处理（VA `0x004C2290`）

**结果：** 编辑器捕获 `cmp ax, 0x2E`（字符 `.`）后，内部调用
`FN_INSERT_TEXT(0x0202006D, 文本, asKeyboardInput=1)`——即**插入文本与键盘输入走同一解析管道**，
`.子程序 xxx` 这类指令行会被正常解析为新子程序，而不是塞进当前单元格的纯文本。

**收益：** 为"代码页追加子程序"的 `FN_INSERT_TEXT(".子程序 名\r\n")` 路线提供了正确性依据
（AutoLinker 生产代码同款用法）。该路线成为有尾随语句行页面的首选创建方式
（1.6.9 的 Path A）；新程序集页无尾随行时则由 Path B（`FN_INSERT_NEW_SUB` + 改名）兜底。

### 1.4 功能码 = 内部编辑器命令（常量扫描总收益）

capstone 扫描确认全部功能码常量真实存在于本 build：

| 功能码 | 命中 | 含义 |
| --- | --- | --- |
| `0x02020071` SET_AND_COMPILE | 1 处（push） | 1.2 的提交路径 |
| `0x0202006D` INSERT_TEXT | 4 处（push） | 键盘管道 + 其他内部调用 |
| `0x01010042` MOVE_CARET | 1 处（push） | IDE 内部跳转同款 |
| `0x0503000A` GET_PRG_TEXT | 1 处（cmp，即分发器） | 1.1 的处理器 |
| `0x02020009` INSERT_NEW_SUB / `0x0202000D` INSERT_NEW_MOD | 各 2 处 | 插入菜单的同源命令 |
| `0x02020070` PRE_COMPILE | 1 处 | 可选预编译验证 |

结合静态分析记录中"菜单命令 ID ↔ 内部编辑器命令"映射表（如菜单 32782/0x800E ↔ 0x0202000D），
证明 **WM_COMMAND 菜单路由与 NES_RUN_FUNC 功能码最终汇入同一分发器**——这解释了
为什么"原生菜单插程序集"与"公开接口编辑代码"能无缝配合，也是插件"菜单建页 + API 写内容"
组合架构的依据。

**收益：** 插件最终形态完全建立在公开接口上（零硬编码地址、零内存补丁），
每个调用都有"IDE 自己也这么调"的背书，换打包/加壳环境时鲁棒性最高。

### 1.5 静态分析存量资产（见 `docs/e595_静态分析_2026-08-28.md`）

- 菜单命令表（插入程序集 32782、全选 33009、复制 57634、粘贴 57637 等）→ 插件保留
  "原生菜单插入程序集"这一条腿（比 `FN_INSERT_NEW_MOD` 多了正确的页面激活行为）。
- 12 个内部文本包函数特征码全部命中（`text_package_parse 0x4D59B0` 等）→
  留作未来做"隐藏页写入/批量注入"的弹药，当前公开接口已够用，刻意不引入。
- 设置活动编辑器 `0x0047ADF0`（`__thiscall(host, editor, mode)`，字段 `host+0x464`）→
  未来若需跨页操作非活动页时的钥匙，当前未用。

## 2. 行为级发现（非反汇编，但同属逆向成果）

| 发现 | 证据 | 收益 |
| --- | --- | --- |
| 整页粘贴会忽略 `.程序集` 名称行 | 0.8–1.6.5 多轮日志：粘贴 74/91 字节后子程序进去了、名字纹丝不动 | 彻底放弃剪贴板路线（1.6.6 起零剪贴板） |
| 粘贴后紧跟的全选/复制会随机丢失（剪贴板序号不变 → 回声假成功） | 19:07 日志 attempt2 `clipboard_changed=0` 却"验证通过" | 确立"序号必须变化才算读到"规则；该规则随剪贴板路线一并退役，但取证思想保留 |
| `WM_MDIGETACTIVE` 会被视觉标签层干扰 | 1.6.7 日志：`maximize_document` 已出现新 hwnd，轮询 30 次全失败 | 新页检测改用 MDI 子窗口快照 diff（1.6.8），从机制上免疫 |
| 代码页网格布局：r0 全表头（空文本、ti=1），r1c0=程序集名；子程序块=表头行+名称行 | 1.6.7/1.6.9 `page_dump`：`r1c0{t=580,n=8,x="程序集1"}`、`r3c0{t=686,x="子程序1"}` | 修正 `matchAnyText` 空表头遮蔽 bug（曾导致改名目标指向表头）；名称定位从此精确 |
| 新程序集页没有尾随空行 | 1.6.9 日志 `no_trailing_row caret=1/0`（MOVE_CARET(2,0) 被吸附回 1/0） | 光标读回验证（`FN_GET_CARET_ROW_INDEX`）拦截无效落点，自动切换 `FN_INSERT_NEW_SUB` 路径 |

## 3. 工具链备注

- 静态扫描/反汇编：Python 3.13 + capstone 5.0.7 + pefile（无需 IDA 即可完成本轮全部验证；
  IDA 9.2 可用于后续更深层的结构体还原）。
- 所有 VA 均为首选基址；插件运行时若需使用，必须按 `GetModuleHandle(nullptr)` 实际基址换算。
- e5.95 其他构建版本不保证地址一致——这就是插件坚持"只依赖公开功能码 + 行为验证"的原因。
