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

---

## 功能模块二：中断与异常处理功能

[PROMPT]
**任务**：在工作区的 `Nk_Os_Lab/code` 项目中修改文件 `kern/trap/trap.c`，实现中断与异常处理功能模块中的 `interrupt_handler(struct trapframe *tf)` 和 `exception_handler(struct trapframe *tf)` 两个函数。这两个函数由现有的 `trap_dispatch()` 调用，分别负责处理 RISC-V Trap 中的中断和同步异常。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。只修改 `kern/trap/trap.c`，将实现插入到 `//lab3 function2 YOUR CODE` 标记附近，并确保两个函数的定义位于 `trap_dispatch()` 调用它们之前，避免隐式函数声明。保留文件中已有的 `print_ticks()`、`idt_init()`、`print_trapframe()`、`print_regs()`、`trap_dispatch()` 和 `trap()` 等既有代码及其行为，不要删除或重写无关逻辑，不要修改 `trapentry.S`、`trap.h`、`clock.c`、`init.c` 或测试与评分脚本。允许添加仅在本文件内部使用的 `static` 辅助函数。

**输出要求**：直接进行文件操作。严格使用下面 `[RELY]` 中与当前工程一致的真实宏、数据结构和函数；不要引入未定义的标识符，不要引入与当前 Lab3 无关的系统调用、调度或关机功能。实现后应能通过编译，并使时钟中断、软件中断回退、未知中断回退、断点异常、非法指令异常和未知异常回退能够正确返回到统一的 Trap 返回流程。

[RELY]

// 宏定义
```c
#define TICK_NUM 100

#define IRQ_S_SOFT   1
#define IRQ_S_TIMER  5

#define CAUSE_ILLEGAL_INSTRUCTION 0x2
#define CAUSE_BREAKPOINT          0x3
```

RISC-V 的 `scause` 最高位为 1 表示该 Trap 是中断，低 63 位表示中断原因；最高位为 0 表示同步异常。`trap_dispatch()` 已使用 `(intptr_t)tf->cause < 0` 判断中断，因此传给 `interrupt_handler()` 的 `tf->cause` 通常包含第 63 位。处理中断原因时应先屏蔽第 63 位，不能直接用带有高位标志的完整值进行 `switch`。

// 数据结构
```c
struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};
```

其中：

- `tf->cause`：当前异常或中断的原因。
- `tf->epc`：Trap 返回后将恢复到的程序计数器。处理同步异常时，应通过修改 `tf->epc` 跳过已经触发异常的指令。
- `tf->status`、`tf->badvaddr` 和 `tf->gpr`：保存的 Trap 上下文，本任务只读使用。

```c
extern volatile size_t ticks;
```

`ticks` 已由 `clock_init()` 初始化为 0，并在每次 S 模式时钟中断发生时递增。

// 可用函数
```c
static void print_ticks(void);
void print_trapframe(struct trapframe *tf);
void clock_set_next_event(void);
int cprintf(const char *fmt, ...);

static inline void trap_dispatch(struct trapframe *tf);
```

- `print_ticks()` 已实现，每调用一次输出 `100 ticks`。
- `print_trapframe(tf)` 已实现，用于输出完整 Trap frame。
- `clock_set_next_event()` 已实现，用于设置下一次时钟事件。
- `trap_dispatch()` 已根据 `tf->cause` 的最高位选择调用 `interrupt_handler()` 或 `exception_handler()`。
- `cprintf()` 支持 `%d`、`%x`、`%lx` 和 `%s` 等格式。

当前工程中没有定义 `SHUTDOWN_AFTER`，当前 Lab3 评分要求也要求持续处理时钟中断直到至少 200 个 tick，因此本任务不要引用 `SHUTDOWN_AFTER`，不要在本模块中调用 `sbi_shutdown()`。

[GUARANTEE]

必须实现或修改的接口：
```c
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
```

这两个函数必须具有外部链接，不能声明为 `static`，因为其他编译单元可能引用它们。函数签名不得修改。

你可以根据需要添加：
- 辅助函数，例如将中断原因与第 63 位分离、根据指令编码计算指令长度的函数；辅助函数建议设为 `static`
- 本文件内部使用的宏定义
- 本文件内部使用的静态变量

[SPECIFICATION]

## interrupt_handler

**Pre-Condition**: 
- `tf` 是有效的 `struct trapframe *`。
- 该函数由 `trap_dispatch()` 在识别到中断时调用；此时 `tf->cause` 通常包含第 63 位中断标志。
- 某些测试或内部调用也可能直接传入只包含低 63 位原因码的 `tf->cause`，实现应能正确提取中断原因。

**Post-Condition**: 
- 根据屏蔽第 63 位后的中断原因完成相应处理，并返回到 `trap_dispatch()` 和后续的 Trap 返回流程。
- 时钟中断路径中，每次调用都必须使 `ticks` 增加 1，并保证下一次时钟事件已经被设置。
- 除 `ticks` 的递增和必要的下一次时钟事件设置外，不得修改 `tf` 中保存的上下文。
- 不得在处理函数中主动开启全局中断，不得修改 `trap_dispatch()` 的分发逻辑，也不得直接调用 `sret`。

**Case 1**:
- 当前中断原因是 `IRQ_S_TIMER` 时，执行一次时钟 tick 统计，并设置下一次时钟事件；每次 `ticks` 达到 `TICK_NUM` 的正整数倍时调用已有的 `print_ticks()`。下一次时钟事件必须在函数返回前设置，不能只在第一次中断时设置。处理结束后正常返回，不调用关机接口，不在函数内主动开启中断。

**Case 2**:
- 当前中断原因是 `IRQ_S_SOFT` 时，仅输出一行完全匹配 `Supervisor software interrupt\n` 的提示，不增加 `ticks`，不重新设置时钟事件，然后正常返回。

**Case 3**:
- 当前中断原因不是 `IRQ_S_TIMER` 或 `IRQ_S_SOFT` 时，先输出一行格式为 `Unknown interrupt: cause %d\n` 的信息，其中 `%d` 使用屏蔽最高位后的原因码；随后调用已有的 `print_trapframe(tf)` 输出完整 Trap frame，并正常返回，不得在该分支中 `panic` 或进入死循环。

**Requirements**: 
- 中断原因必须通过与 `0x7fffffffffffffff` 等价的 64 位掩码或等价的位操作得到，不能把 `0x8000000000000005` 直接当作中断原因 5 使用。
- 每次 S 模式时钟中断都必须调用 `clock_set_next_event()`，确保中断能够持续触发。
- 时钟统计应基于全局 `ticks`；不得自行创建另一套独立计数变量。
- 两个处理函数必须能够在正常处理后返回，使 `trap()` 返回 `__trapret`，最终由 `RESTORE_ALL` 和 `sret` 完成返回。
- 不得修改 `print_ticks()` 的既有行为，也不得引用当前工程中未定义的 `SHUTDOWN_AFTER`。

## exception_handler

**Pre-Condition**: 
- `tf` 是有效的 `struct trapframe *`。
- 该函数由 `trap_dispatch()` 在识别到同步异常时调用。
- `tf->cause` 的最高位为 0，低 63 位保存同步异常原因。
- `tf->epc` 保存触发异常的指令地址。

**Post-Condition**: 
- 根据 `tf->cause` 完成异常分类和处理，并返回统一的 Trap 返回流程。
- 对于断点和非法指令异常，`tf->epc` 必须推进到异常指令之后，避免 `sret` 后再次执行同一条指令。
- 对于其他异常，输出当前 Trap frame，但不得修改函数签名或在默认分支中终止系统。

**Case 1**:
- 当前异常原因是 `CAUSE_BREAKPOINT` 时，先输出一行包含 `Breakpoint at 0x` 和原始 `tf->epc` 的提示，再根据当前指令的 2 字节或 4 字节编码将 `tf->epc` 推进相应长度。输出地址后不得再次原样返回同一指令。

**Case 2**:
- 当前异常原因是 `CAUSE_ILLEGAL_INSTRUCTION` 时，先输出一行包含 `Illegal instruction at 0x` 和原始 `tf->epc` 的提示，再根据当前非法指令的 2 字节或 4 字节编码将 `tf->epc` 推进相应长度。当前评分代码使用的是 4 字节非法指令，但实现应根据指令低两位编码判断实际长度，不要不加判断地假定所有异常都固定推进 4 字节。

**Case 3**:
- 当前异常原因不是 `CAUSE_BREAKPOINT` 或 `CAUSE_ILLEGAL_INSTRUCTION` 时，调用已有的 `print_trapframe(tf)` 输出完整 Trap frame，并正常返回。该分支不得输出后继续执行出错指令，不得直接修改 `sepc` CSR，也不得通过 `panic` 或死循环阻止测试继续运行。

**Requirements**: 
- 必须通过修改 `tf->epc` 调整返回位置，不能直接写 `sepc`；后续 `RESTORE_ALL` 会根据 Trap frame 中的 `epc` 恢复 `sepc`。
- 指令长度判断应遵循 RISC-V 指令编码规则：根据故障指令最低两位判断是否为 2 字节压缩指令；非法指令和断点指令的具体处理应使用实际长度推进。
- 不得修改 `tf->cause`。
- 不得修改 `trap_dispatch()`、`trap()`、Trap frame 布局或汇编入口。
- 未知异常回退分支必须调用 `print_trapframe(tf)` 后返回，以便评分能够检查未知异常的 `cause 0x00000014` 输出。
- 两个处理函数都必须能够在正常处理后返回，使 `trap()` 返回 `__trapret`，最终由 `RESTORE_ALL` 和 `sret` 完成返回。