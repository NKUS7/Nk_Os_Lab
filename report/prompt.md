#### 功能模块一
[PROMPT]
**任务**：修改文件 `kern/trap/trapentry.S`，实现或完善 `RESTORE_ALL` 与 `__alltraps`。`RESTORE_ALL` 负责根据已有 `SAVE_ALL` 建立的 Trap frame 恢复被打断程序的处理器上下文；`__alltraps` 负责在 Trap 入口完成上下文保存后，按照 RISC-V 调用约定把当前 Trap frame 地址作为 `struct trapframe *` 参数传递给 C 语言函数 `void trap(struct trapframe *tf)`，并在 `trap()` 返回后进入 `__trapret`，最终通过 `sret` 返回原执行流。

**操作要求**：你必须在项目中修改实际文件 `kern/trap/trapentry.S`，保留文件中的现有代码，只在你需要补全或完善的 `RESTORE_ALL`、`__alltraps` 及其附近位置进行修改和补充。不要删除既有逻辑，不要修改无关文件，不要修改 `pmm.c`。必须先阅读并严格依据文件中已有的 `SAVE_ALL`、`struct trapframe` 定义、`__trapret` 以及 `trap()` 的函数签名进行实现；不得自行假设与实际 Trap frame 不一致的寄存器保存顺序、偏移或数据布局。不得修改 `SAVE_ALL`、`struct trapframe` 和 `trap()` 的函数签名。实现方式不限定为某一种固定汇编指令序列，但必须保证“Trap 进入 -> 上下文保存 -> C 语言处理 -> 上下文恢复 -> Trap 返回”闭环一致。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 部分提供的信息，以工程真实代码为准，不要引入无关功能。

[RELY]

// 1. 相关文件与已有汇编接口

`kern/trap/trapentry.S` 中已提供或需要完善的汇编符号/宏包括：

SAVE_ALL      // 已提供：把 Trap 发生时的处理器状态保存到当前内核栈上的 Trap frame 中
__alltraps    // 需要完善：Trap 汇编入口，建立 Trap frame 并调用 trap(struct trapframe *tf)
__trapret     // 已提供：Trap 返回路径，最终执行 sret
RESTORE_ALL   // 需要实现/完善：从当前 Trap frame 恢复处理器上下文

// 2. C 语言接口与数据结构

struct trapframe;                  // 决定 SAVE_ALL 保存后的上下文数据布局
void trap(struct trapframe *tf);   // C 语言 Trap 处理函数，不得修改函数签名

// 3. RISC-V 体系结构与调用约定

- RISC-V 函数调用约定：第一个整型/指针参数通过 a0 传递。
- `__alltraps` 调用 `trap()` 前，必须把当前 Trap frame 的首地址按调用约定放入 a0。
- `trap()` 返回后，应进入 `__trapret`，由 `__trapret` 继续完成返回流程并执行 `sret`。
- Trap frame 的实际布局由 `struct trapframe` 和已有 `SAVE_ALL` 决定，不得臆造。
- 特别注意：`SAVE_ALL` 中对 x2(sp) 的处理方式与其他通用寄存器并不完全相同；`RESTORE_ALL` 不能简单地把所有寄存器按完全相同方式恢复，必须先以当前 Trap frame 基址完成其他必要恢复，最后安全恢复被打断程序的 sp，或采用与现有 SAVE_ALL 布局等价的正确方式。
- `RESTORE_ALL` 必须与 `SAVE_ALL` 的保存关系一一对应，不能破坏 `__trapret` 执行 `sret` 所需的返回状态。

[GUARANTEE]

必须实现或完善以下接口：

```asm
__alltraps
RESTORE_ALL
```

可以添加：
- 内部使用的汇编标签
- 必要的注释
- 与现有 `SAVE_ALL` 布局一致的内部宏或辅助代码片段

但不得：
- 修改 `SAVE_ALL`
- 修改 `struct trapframe`
- 修改 `void trap(struct trapframe *tf)` 的函数签名
- 修改 `__trapret` 既有的 `sret` 返回机制
- 引入与 Trap 上下文保存/恢复无关的功能

[SPECIFICATION]

## __alltraps
**Pre-Condition**：
- `SAVE_ALL` 已按现有 Trap frame 布局把 Trap 发生时的处理器状态保存到当前内核栈。
- 当前栈指针指向该 Trap frame。
- `trap(struct trapframe *tf)` 可被调用。
- `__trapret` 已存在，并负责最终 Trap 返回。

**Post-Condition**：
- `__alltraps` 在完成必要的 Trap 入口处理后，将当前 Trap frame 首地址按照 RISC-V 调用约定放入 `a0`。
- 调用 `trap(struct trapframe *tf)`。
- `trap()` 返回后，跳转进入 `__trapret`。
- 调用 `trap()` 过程中不得破坏后续 `RESTORE_ALL`/`__trapret` 恢复上下文所需的信息。

**Case 1**：正常 Trap 入口调用
- 触发条件：已存在有效 Trap frame，`trap()` 可调用。
- 执行行为：正确建立 `a0 = 当前 Trap frame 地址`，调用 `trap()`；返回后进入 `__trapret`。

**Case 2**：不得臆造 Trap frame 布局
- 触发条件：`SAVE_ALL` 或 `struct trapframe` 已有实际布局。
- 执行行为：必须以文件中真实保存顺序和偏移为准，不得按固定伪代码假设布局。

## RESTORE_ALL
**Pre-Condition**：
- 当前 `sp` 指向 `SAVE_ALL` 建立的 Trap frame。
- Trap frame 中各寄存器保存位置由已有 `SAVE_ALL` 和 `struct trapframe` 决定。

**Post-Condition**：
- 按与 `SAVE_ALL` 对应的关系恢复 Trap frame 中保存的处理器上下文。
- 对 x2(sp) 进行特殊处理：不得在仍需要 Trap frame 基址时把 sp 当普通寄存器提前恢复；应保证最终恢复到被打断程序的 sp，或采用与现有布局等价的正确方式。
- 不破坏 `__trapret` 执行 `sret` 所需的状态。
- 恢复完成后，控制流可进入 `__trapret` 并最终通过 `sret` 返回原执行流。

**Case 1**：正常上下文恢复
- 触发条件：Trap frame 布局与 `SAVE_ALL` 一致。
- 执行行为：正确恢复已保存寄存器；x2(sp) 按特殊方式安全恢复；不得破坏返回路径。

**Case 2**：布局依赖不明确
- 触发条件：无法从现有 `SAVE_ALL`、`struct trapframe` 或文件骨架确定某一偏移/顺序。
- 执行行为：不得猜测硬编码；应以工程真实代码为准补全，并通过注释明确依赖关系；不得引入无关功能。

**Case 3**：不得修改既有接口
- 触发条件：实现 `RESTORE_ALL` 和 `__alltraps` 时涉及 `SAVE_ALL`、`struct trapframe`、`trap()` 或 `__trapret`。
- 执行行为：保持 `SAVE_ALL`、`struct trapframe`、`trap()` 签名和 `__trapret` 既有返回机制不变，只补全本模块要求的上下文恢复与 Trap 入口调用关系。