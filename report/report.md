# 操作系统实验报告

## 实验基本信息

| 项目 | 内容 |
|------|------|
| **实验名称** | Lab 4: [进程管理] |
| **小组成员** | 2410796-艾思奇、2412806-阳雨洁 |
| **完成日期** | 2026-11-11 |

### 小组分工

| 成员 | 负责的练习/模块 |
|------|----------------|
| 2410796-艾思奇 |  |
| 2412806-阳雨洁 | 功能模块一、功能模块二 |


---

## 一、实验目的

<!-- 说明本次实验的主要目标和预期成果 -->

本实验的主要目的是：

1. [理解 Sv39 多级页表的结构，掌握运行时建立、更新与撤销虚拟地址映射的方法]
2. [理解进程控制块（PCB）、内核栈、trapframe 与 context 的组织方式]
3. [掌握 idleproc 与 initproc 的创建流程，理解 kernel_thread → do_fork → copy_thread 的首次运行链路]
4. [理解内核线程与用户进程的区别：内核线程仅运行在内核态、共享内核地址空间]
5. [理解进程调度与上下文切换的基本原理，为后续实验中的用户进程、系统调用与缺页处理打基础]

---

## 二、实验环境

你们使用的 AI 工具

| 成员 | AI 编程工具 | 底层模型 | 备注 |
|------|------------|---------|------|
| 2410796-艾思奇 | vscode的codex插件 | [DeepSeek-V4-Flash] | 无 |
| 2412806-阳雨洁 | vscode的claude插件 | [DeepSeek-V4-Flash] | 无 |

**说明：**

- **AI 编程工具**：指具体使用的终端工具、编辑器插件、桌面应用或浏览器界面
- **底层模型**：指该工具使用的大语言模型及版本

---

## 三、实验整体逻辑分析

### 2.1 本章节的逻辑主线

- 本章实验的核心主题是操作系统的进程与线程管理。在前面的实验中，内核已经具备物理内存管理、基础页表机制和 Trap 处理能力，但仍然以单一执行流的方式运行，无法并发执行多个任务，也没有体现虚拟内存机制在进程/线程隔离中的作用。

- Lab4 在此基础上完成两方面扩展：一是进一步完善虚拟内存管理，引入 `mm_struct` / `vma_struct`，为每个执行实体提供逻辑上的地址空间（本实验采用预映射方式，不涉及按需分页和页面置换）；二是引入内核线程机制，实现进程控制块、上下文切换和调度器，让多个内核线程轮流使用 CPU。

- 需要特别注意：ucore 对线程和进程做了统一处理，都用 `proc_struct` 表示。由于所有内核线程共享同一个内核地址空间，它们从属于同一个唯一的内核进程。本实验创建的是内核线程，`current->mm == NULL`，`copy_mm()` 只保留接口形态。

| 比较项 | 内核线程 | 用户进程 |
|--------|----------|----------|
| 运行模式 | 仅在内核态运行 | 在用户态和内核态之间切换 |
| 地址空间 | 共享内核地址空间 | 拥有独立的用户虚拟地址空间 |

- 本实验的最终目标是：系统启动后至少存在 `idleproc` 与 `initproc` 两个关键线程，并在调度器作用下让 `initproc` 被真正切换执行。

### 2.2 功能的逐步实现

本章实验的推进顺序是：先补齐前序实验代码，再实现动态页表映射，随后构造 PCB 与内核线程，最后实现调度与上下文切换。具体步骤如下：

    0. 前置工作：补齐 Lab2 / Lab3 的代码

        做了什么：把 Lab2 的 page_init()、Best-Fit 物理内存分配器，以及 Lab3 的 Trap 入口/上下文恢复与中断/异常处理代码补充到 Lab4 工程中。

        为什么先做这个：Lab4 的 pmm_init()、时钟中断和进程切换都建立在物理内存管理、页表机制和 Trap 闭环之上；如果前置代码缺失，内核无法完成初始化，也无法进入调度流程。

    1. 实现动态页表映射（功能模块一：page_insert / page_remove

        做了什么：在 get_pte() 定位页表项的基础上，实现 page_insert() 建立/更新“虚拟地址 → 物理页”的映射，实现 page_remove() 幂等撤销映射，并维护物理页引用计数、PTE 权限位和 TLB 一致性。

        为什么接着实现这个：进程/线程的地址空间最终由页表描述；没有运行时建立和撤销映射的能力，就无法维护进程地址空间，也无法通过 check_pgdir() 等页表自检。

    2. 实现 PCB 与内核线程创建（功能模块二：alloc_proc / proc_init / kernel_thread / do_fork / copy_thread / copy_mm）

        做了什么：实现 PCB 的分配与初始化；把当前执行现场包装成 idleproc；通过 kernel_thread(init_main, ...) 创建 initproc；完成 do_fork 的资源分配、内核栈建立、trapframe/context 构造、PID 分配、链表挂接和状态置位。

        为什么然后实现这个：调度器必须要有可调度的 PCB；idleproc 和 initproc 是后续所有进程/线程创建与调度的起点。

    3. 实现调度与上下文切换（功能模块三：schedule / proc_run / switch_to / cpu_idle）

        做了什么：实现 schedule() 的 Round-Robin 选择逻辑——先清除 current->need_resched；若 current 不是 idleproc，则从 current->list_link 的下一个节点开始遍历 proc_list；若 current 是 idleproc，则从链表头开始；找到第一个 PROC_RUNNABLE 的线程作为 next，若遍历完仍未找到则回退到 idleproc。实现 proc_run()——把 current 指向 next，切换页表（RISC-V 下由 next->pgdir 设置 satp），并调用 switch_to(&prev->context, &next->context) 完成上下文切换。实现 switch.S 中的 switch_to——按 struct context 的字段顺序保存 ra/sp/s0~s11，再从目标 context 恢复。实现 cpu_idle()——循环检查 current->need_resched，必要时调用 schedule()，使 CPU 在没有其他就绪线程时停留在 idle 循环。

        为什么最后实现这个：只有在 PCB 与内核线程创建完成后，调度器才有对象可选；上下文切换也是验证“多执行流并发”的最后一步。调度器通过 proc_list 把 idleproc、initproc 以及后续创建的线程统一管理起来，使系统从单一执行流真正进入多线程轮转。

    4. 运行与评测

        做了什么：在模块三完成后运行 make qemu，观察内核启动、initproc 创建以及时钟中断输出；运行课程提供的测试（make grade），验证物理内存管理、进程创建和调度相关测试；保存运行与评分截图作为实验验证材料。

---

## 四、实验内容与实现

<!-- 
说明：本部分按照 exercises 文档中的练习顺序组织
每个功能模块或者练习中可能包含多项任务，请根据实际情况调整
-->

### 前置工作：补齐 Lab2 / Lab3 代码

**负责人：** [2412806-阳雨洁]

#### 模块功能描述

Lab4 的工程骨架中保留了 `//lab2`、`//lab3` 标记，需要把前序实验已经完成的内容回填，保证 Lab4 能在 Lab2/Lab3 的可用状态上继续推进。

**需要补回的内容：**

```c
// kern/mm/pmm.c
static void page_init(void);

// kern/mm/default_pmm.c
// Best-Fit 分配器的 init / init_memmap / alloc_pages / free_pages / nr_free_pages
// 以及 default_pmm_manager 注册

// kern/trap/trap.c
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
static inline void trap_dispatch(struct trapframe *tf);

// kern/trap/trapentry.S
.macro RESTORE_ALL
__alltraps
```

**功能说明：**

- `page_init()`：从 DTB 读取物理内存范围，建立 `pages` 描述符数组，把内核镜像、OpenSBI 和描述符数组自身标记为 Reserved，再把剩余空闲页通过 `init_memmap()` 注册给 PMM。
- Best-Fit 分配器：空闲块按物理地址有序组织；分配时扫描所有块，选择满足 `property >= n` 的最小空闲块；释放时按地址插入并合并前后相邻空闲块。
- `interrupt_handler()` / `exception_handler()`：分别处理时钟中断、软件中断、断点异常、非法指令异常，并保证未知中断/异常走可返回的打印兜底路径。
- `RESTORE_ALL` / `__alltraps`：按 `SAVE_ALL` 的实际布局恢复通用寄存器与 CSR，并在 Trap 入口把 trapframe 地址放入 `a0` 后调用 `trap()`。

**补充说明：**

- `default_pmm.c` 原有的 `basic_check()` 与 `best_fit_check()` 属于框架测试代码，未作修改。
- `default_pmm.c` 中包含 `<best_fit_pmm.h>`，而 Lab4 工程原本缺少该头文件，因此按 Lab3 内容补入了 `kern/mm/best_fit_pmm.h`。
- Lab4 的 `default_pmm_manager` 使用 Best-Fit 语义，`check` 指向已有的 `best_fit_check()`。

#### 实现结果

- `pmm.c`、`default_pmm.c`、`trap.c`、`trapentry.S` 均使用 Lab4 Makefile 的参数单独编译通过。
- 该部分完成后，Lab4 具备了物理内存管理、页表自检和 Trap 处理的基础，可以继续实现 Lab4 的页表映射与进程管理。

---

### 功能模块一：[页表映射的建立与撤销]

**负责人：** [2412806-阳雨洁]

#### 模块功能描述

**需要实现/修改的函数：**

```c
static inline void page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep);
void page_remove(pde_t *pgdir, uintptr_t la);
int  page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
```

**功能说明：**

本模块负责在现有多级页表框架上建立、更新和撤销“线性地址 → 物理页”的映射，是虚拟内存管理与进程地址空间能够工作的基础。

    该模块在整个系统中的作用：

        定位页表项：page_insert/page_remove 都必须通过 get_pte() 定位 la 对应的 PTE；page_insert 使用 create=1，必要时分配中间级页表页；page_remove 使用 create=0，不分配新页表。

        建立/更新映射：page_insert 成功定位 PTE 后，对新物理页 page_ref_inc()，再把 PTE 写为 pte_create(page2ppn(page), PTE_V | perm)，从而同时设置物理页号、有效位和调用者要求的权限位。

        撤销映射：page_remove 在 PTE 有效时取出对应物理页，清空 PTE，调用 page_ref_dec()；引用计数降为 0 时 free_page() 释放物理页，并执行 tlb_invalidate()。

    需要特别处理的场景：

        原地址已映射同一物理页：page_insert 先 page_ref_inc() 再 page_ref_dec()，净效果为 0，只更新权限位，避免同一页被重复计数。

        原地址已映射其他物理页：先对新页 page_ref_inc()，再通过 page_remove_pte() 撤销旧映射（递减旧页引用、必要时释放、清 PTE、失效 TLB），最后写入新 PTE。

        中间级页表分配失败：get_pte(..., 1) 返回 NULL 时直接返回 -E_NO_MEM，此时尚未修改引用计数或页表，不会留下半完成映射。

        幂等删除：page_remove 对 get_pte(..., 0) 的 NULL 结果、以及 *ptep 无效的情况都会安静返回；重复调用不会重复递减引用计数。

        TLB 一致性：任何 PTE 写入（新建、更新、清空）之后，都对 la 执行 tlb_invalidate()，避免硬件继续使用旧翻译。

    与其他模块的交互关系：

        上游：get_pte() 负责 Sv39 三级页表遍历与中间级页表分配；pte2page() 把 PTE 还原为 struct Page；page_ref_inc()/page_ref_dec() 维护引用计数。

        下游：check_pgdir()、check_boot_pgdir() 和 test_lab4_pmm_basic() 通过 page_insert/page_remove 验证映射、引用计数和权限位的正确性。

        与进程的关系：进程的 pgdir（页目录物理地址）最终由这些映射接口维护；本实验创建的是内核线程，暂时共享内核页表。


#### 最终提示词

完整提示词见 `report/prompt.md` 的「功能模块一」，核心任务如下：

````markdown
[PROMPT]
**任务**：修改文件 `kern/mm/pmm.c`，实现 `page_insert` 与 `page_remove` 两个函数。`page_insert` 负责在现有多级页表框架上建立或更新“线性地址 → 物理页”的映射，并同步维护物理页引用计数、页表项权限位与 TLB 状态；`page_remove` 负责安全撤销已建立的映射，作为幂等删除接口，同时正确递减引用计数并失效 TLB。

**操作要求**：你必须在项目中修改实际文件 `kern/mm/pmm.c`，保留文件中的现有代码，只在 `page_insert`、`page_remove` 及其附近位置进行修改和补充。不要删除既有逻辑，不要修改无关文件，不要修改 `trapentry.S`、`swap.c` 等无关模块。必须先阅读并严格依据工程中已有的 `get_pte`、`pte_create`、`pte2page`、`page_ref_inc`、`page_ref_dec`、`set_page_ref`、`alloc_page`、`free_page`、`tlb_invalidate`、`flush_tlb`、`struct Page`、`struct Page *pages`、`boot_pgdir_va`、`PDE/PTE` 类型定义以及 `PTE_V/PTE_U/PTE_W/PTE_R` 等宏进行实现；不得自行假设与实际页表布局、权限位或引用计数语义不一致的行为。不得修改上述已有接口的签名、语义和 `struct Page` 的定义。实现方式不限定为某一种固定代码序列，但必须保证“定位页表项 → 建立/更新或删除映射 → 维护引用计数 → 失效 TLB → 返回状态”的闭环一致。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 部分提供的信息，以工程真实代码为准，不要引入无关功能。
````

#### 实现迭代过程

本模块的实现经历了 [1] 次编码迭代，过程如下：

---

##### 第一次迭代

实现前先阅读了 `kern/mm/pmm.c`、`kern/mm/pmm.h`、`kern/mm/mmu.h` 和 `kern/mm/memlayout.h`，确认了以下事实：

- `get_pte(pgdir, la, create)` 已经完成 Sv39 三级页表遍历；`create=1` 时会调用 `alloc_page()` 分配中间级页表，并可能返回 `NULL`。
- `pte_create(ppn, type)` 的参数是 PPN，并且内部已经执行 `(ppn << PTE_PPN_SHIFT) | PTE_V | type`。
- `page_ref_dec(page)` 只返回递减后的引用计数，不会自动释放物理页。
- `free_page(page)` 是 `free_pages(page, 1)` 的宏。
- `tlb_invalidate(pgdir, la)` 使用 `sfence.vma` 失效单个地址的 TLB。

据此实现：

- `page_remove_pte()`：PTE 有效时取得物理页，`page_ref_dec()`；计数为 0 时 `free_page()`；清空 `*ptep`；执行 `tlb_invalidate()`。
- `page_remove()`：`get_pte(..., 0)` 判空后调用 `page_remove_pte()`，因此中间级页表不存在、PTE 无效或重复删除都安全返回。
- `page_insert()`：先 `get_pte(..., 1)`，失败返回 `-E_NO_MEM`；`page_ref_inc()` 后检查旧 PTE：同一页则 `page_ref_dec()` 抵消，不同页则调用 `page_remove_pte()` 撤销旧映射；最后写入新 PTE 并 `tlb_invalidate()`。

**遇到的问题：**

- 问题一：把 `pte_create` 描述为接收物理地址 `pa` 和 `perm`，与工程真实接口不一致。
- 原因分析：工程中的 `pte_create` 接收的是 PPN，并且已经包含 `PTE_V`；如果按提示词直接传 `page2pa(page)`，会导致页号被重复左移。
- 问题二：认为 `page_ref_dec` 可能触发释放，但工程实现只递减计数。
- 原因分析：引用计数接口与释放策略是分离的；需要在本模块内判断计数是否归零并调用 `free_page()`。

**问题解决策略：**

- 针对问题一：以工程真实代码为准，使用 `pte_create(page2ppn(page), PTE_V | perm)`。
- 针对问题二：在 `page_remove_pte()` 中显式判断 `page_ref_dec(page) == 0`，再调用 `free_page(page)`。
- 针对同一页重复映射：先 `page_ref_inc()` 再 `page_ref_dec()`，保证净引用计数不变，同时统一走“写 PTE + 失效 TLB”的路径。

**最终结果：**

- `kern/mm/pmm.c` 使用 Lab4 Makefile 的编译参数单独编译通过。
- 完成本模块后，完整链接中 `page_insert`、`page_remove` 的未定义符号全部消失。
- 从接口行为上满足 `check_pgdir()`、`check_boot_pgdir()` 与 `test_lab4_pmm_basic()` 对映射、引用计数、权限位和 TLB 一致性的要求。

**关键改进点总结：**

1. [以工程真实接口为准：`pte_create` 传 PPN 并依赖其内置的 `PTE_V`，没有按提示词的伪接口臆造。]
2. [引用计数与释放分离：`page_ref_dec` 只递减，计数为 0 时由本模块显式 `free_page`。]
3. [同一物理页重复映射时净计数为 0，只更新权限位。]
4. [所有 PTE 写入后统一 `tlb_invalidate`，保证 TLB 与页表一致。]
5. [`page_remove` 对不存在/无效映射安静返回，保证幂等性。]

---

### 功能模块二：[内核线程创建与登记]

**负责人：** [2412806-阳雨洁]

#### 模块功能描述

**需要实现/修改的函数：**

```c
static struct proc_struct *alloc_proc(void);
void proc_init(void);
int  kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int  do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int  copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

**功能说明：**

本模块负责把系统从“已经能运行内核代码”推进到“已经能创建并登记多个内核线程”，是调度的前提。

- `alloc_proc()`：通过 `kmalloc` 分配 `proc_struct`，整体清零后设置 `state = PROC_UNINIT`、`pid = -1`、`kstack = 0`、`need_resched = 0`、`parent = NULL`、`mm = NULL`、`tf = NULL`、`pgdir = boot_pgdir_pa`、`flags = 0`，并保证 `context`、`name`、`list_link/hash_link` 处于安全空白状态。
- `proc_init()`：初始化 `proc_list` 与 `hash_list`；创建 `idleproc`（`pid = 0`、`PROC_RUNNABLE`、`kstack = bootstack`、`need_resched = 1`、`pgdir = boot_pgdir_pa`），挂入链表/哈希表，`nr_process++`，置 `current = idleproc`；随后通过 `kernel_thread(init_main, NULL, 0)` 创建 `initproc`，并用 `find_proc()` 取回指针、命名 `"init"`。
- `kernel_thread()`：构造局部 `trapframe` 模板：`epc = kernel_thread_entry`、`s0 = fn`、`s1 = arg`、`a0 = 0`；`status` 从 `sstatus` 读取后置 `SSTATUS_SPP | SSTATUS_SPIE`、清 `SSTATUS_SIE`；调用 `do_fork(clone_flags, 0, &tf)` 并返回 PID。
- `do_fork()`：按“分配 PCB → 设置 parent → 分配内核栈 → copy_mm → copy_thread → 分配 PID 并挂入 proc_list/hash_list → wakeup_proc”的顺序创建线程；前 3 步失败时按逆序释放资源，不挂链表、不递增 `nr_process`。
- `copy_thread()`：在 `proc->kstack + KSTACKSIZE` 顶部放置 `trapframe`，复制模板（`tf == NULL` 时清零）；`a0 = 0`；`gpr.sp = (esp == 0) ? proc->tf : esp`；`context.ra = forkret`，`context.sp = proc->tf`。
- `copy_mm()`：本实验为内核线程，`proc->mm = NULL`，成功返回 0，保留后续扩展接口。

**首次运行链路：**

```text
schedule() / proc_run() 、
    -> switch_to(&prev->context, &next->context)
    -> next->context.ra == forkret
    -> forkret() -> forkrets(current->tf)
    -> __trapret -> RESTORE_ALL -> sret
    -> kernel_thread_entry: move a0, s1; jalr s0
    -> fn(arg)
```

**idleproc / initproc / current 的关系：**

- `idleproc` 不是普通 fork 产物，而是把 `kern_init` 以来的当前执行现场包装成第 0 个内核线程；其 `kstack` 直接复用启动栈 `bootstack`。
- `initproc` 通过 `kernel_thread(init_main, NULL, 0)` 创建，走完整的 `kernel_thread → do_fork → copy_thread` 路径，`parent` 为 `idleproc`。
- `current` 在 `proc_init()` 中指向 `idleproc`，之后由调度器在切换到新线程时更新。

#### 最终提示词

完整提示词见 `report/prompt.md` 的「功能模块二」，核心任务如下：

````markdown
[PROMPT]
**任务**：修改文件 `kern/process/proc.c`，实现 `alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm` 六个函数。本模块负责把系统从“已经能运行内核代码”推进到“已经能创建并登记多个内核线程”：`alloc_proc` 得到一个干净的进程控制块；`proc_init` 把当前执行现场包装成 `idleproc` 并创建第一个真正的内核线程 `initproc`；`kernel_thread` 把函数入口和参数整理成线程首次运行所需的临时上下文；`do_fork` 把上下文、内核栈、PID、链表、状态位等资源组合成一个可运行实体；`copy_thread` 把 trapframe 和 context 安放到新线程自己的内核栈上；`copy_mm` 在当前实验阶段承担地址空间处理接口的占位作用。

**操作要求**：你必须在项目中修改实际文件 `kern/process/proc.c`，保留文件中的现有代码，只在上述函数及其附近位置进行修改和补充。不要删除既有逻辑，不要修改无关文件，不得修改 `switch.S`、`trapentry.S`、`trap.c`、`pmm.c` 等无关模块。必须先阅读并严格依据工程中已有的 `struct proc_struct`、`struct context`、`struct trapframe`、`list_entry_t`、`proc_list`、`hash_list`、`idleproc`、`initproc`、`current`、`nr_process`、`bootstack`、`boot_pgdir_pa`、`kernel_thread_entry`、`forkret`、`forkrets`、`SSTATUS_SPP/SPIE/SIE` 等实际定义与符号进行实现；不得自行假设与实际结构布局、字段偏移、上下文保存顺序或符号名不一致的行为。

**输出要求**：直接进行文件操作，使用 [RELY]、[GUARANTEE]、[SPECIFICATION] 部分提供的信息，以工程真实代码为准，不要引入无关功能。
````

#### 实现迭代过程

本模块的实现经历了 [1] 次编码迭代，过程如下：

---

##### 第一次迭代

实现前先阅读了 `kern/process/proc.c`、`kern/process/proc.h`、`kern/trap/trap.h`、`kern/process/entry.S`、`kern/process/switch.S`、`kern/trap/trapentry.S`、`kern/mm/pmm.h` 与 `kern/sync/sync.h`，确认了以下事实：

- `struct proc_struct` 的字段顺序为 `state/pid/runs/kstack/need_resched/parent/mm/context/tf/pgdir/flags/name/list_link/hash_link`。
- `struct context` 只保存 `ra/sp/s0~s11` 共 14 个 callee-saved 寄存器。
- `kernel_thread_entry` 位于 `kern/process/entry.S`：`move a0, s1; jalr s0; jal do_exit`。
- `forkret` 调用 `forkrets(current->tf)`；`forkrets` 位于 `trapentry.S`，执行 `move sp, a0; j __trapret`。
- `boot_pgdir_pa` 的实际类型是 `uintptr_t`，不是提示词中的 `pde_t *`。
- `alloc_page()` 是无参数宏；`setup_kstack()`/`put_kstack()` 已在文件中提供。
- `local_intr_save/local_intr_restore` 可用于保护 PID 分配与链表挂接。

据此实现：

- `alloc_proc()`：`kmalloc` + `memset` + 逐字段初始化；失败返回 `NULL`。
- `copy_mm()`：内核线程场景下 `proc->mm = NULL`，返回 0。
- `copy_thread()`：在 `proc->kstack + KSTACKSIZE` 顶部放置 trapframe；复制模板或清零；`a0 = 0`；`gpr.sp` 对内核线程设为 trapframe 地址；`context.ra = forkret`、`context.sp = proc->tf`。
- `do_fork()`：7 步创建流程；`alloc_proc`/`setup_kstack`/`copy_mm` 失败时跳转到对应清理标签；PID 分配、`hash_proc`、`list_add`、`nr_process++` 放在关中断临界区；最后 `wakeup_proc()`。
- `kernel_thread()`：构造 trapframe 模板并调用 `do_fork(clone_flags, 0, &tf)`。
- `proc_init()`：初始化 `proc_list`/`hash_list`；创建并登记 `idleproc`，置 `current`；通过 `kernel_thread(init_main, ...)` 创建 `initproc`，`find_proc` 取回并命名。

**遇到的问题：**

- 问题一：把 `boot_pgdir_pa` 写成 `pde_t *`，实际工程中是 `uintptr_t`。
- 原因分析：`proc->pgdir` 也是 `uintptr_t`，直接赋 `boot_pgdir_pa` 即可，不能按指针类型处理。
- 问题二：`copy_thread` 的 `tf` 参数可能为 `NULL`，直接解引用会崩溃。


**问题解决策略：**

- 针对问题一：以工程真实类型为准，`proc->pgdir = boot_pgdir_pa`。
- 针对问题二：在 `copy_thread` 中先判断 `tf != NULL`，否则 memset 整个 trapframe，再设置 `a0/sp/context`。


**最终结果：**

- `kern/process/proc.c` 使用 Lab4 Makefile 的编译参数单独编译通过。
- 完成本模块后，完整链接中 `proc_init` 的未定义符号消失。
- 静态核对 `test_lab4_process_create()` 的断言：`idleproc` 的 `pid/state/kstack/parent/mm/tf/runs/flags/context.ra` 与 `initproc` 的 `pid/state/parent/mm/context.ra/sp` 均已满足。

**关键改进点总结：**

1. [以工程真实结构为准：`boot_pgdir_pa` 是 `uintptr_t`，trapframe/context 字段偏移严格按实际定义。]
2. [首次运行链路完整：`context.ra = forkret`、`context.sp = tf`、`gpr.sp = tf`，与 `forkrets/__trapret/kernel_thread_entry` 配合。]
3. [资源清理干净：`do_fork` 前 3 步失败时逆序释放内核栈与 PCB，不挂链表、不递增计数。]
4. [`copy_thread` 对 `tf == NULL` 做了保护，避免空指针。]
5. [按实验要求登记 idleproc，并记录其与经典 ucore 调度实现的差异。]

---

### 功能模块三：[进程调度与上下文切换]（待完成）

**负责人：** [2410796-艾思奇]

#### 模块功能描述

（待补充：`schedule()`、`proc_run()`、`switch_to()`、`cpu_idle()` 等函数的实现思路，以及与 `proc_list`、`need_resched`、Round-Robin 调度的关系。）

#### 最终提示词

（待补充）

#### 实现迭代过程

（待补充）

---

## 五、测试与验证

<!-- 根据该实验的具体情况，提供完整的测试运行截图，应包含：
- 编译及运行成功的输出（make qemu），可能有多个
- 测试通过的结果（make grade），只有一个
-->

### 1. 阶段性验证（功能模块一、二）

使用 Lab4 Makefile 的原始编译参数，对已修改的文件进行单独编译：

```text
kern/mm/default_pmm.c   COMPILE OK
kern/mm/pmm.c           COMPILE OK
kern/trap/trap.c        COMPILE OK
kern/trap/trapentry.S   ASSEMBLE OK
kern/process/proc.c     COMPILE OK
```

对当前工程做完整链接检查，剩余未定义符号仅有：

```text
undefined reference to `cpu_idle'
```

`page_insert`、`page_remove`、`proc_init` 等本报告已实现模块的符号均已解析；`cpu_idle` 属于功能模块三（调度）范围。


### 2. make qemu 运行截图

（待补充：功能模块三完成后运行 `make qemu`，展示 `idleproc`/`initproc` 的创建与调度输出。）

### 3. make grade 测试结果

（待补充：功能模块三完成后运行 `make grade`，展示 Total Score 截图。）

---

## 六、实验总结与收获

### 对操作系统的理解

本次实验把“进程/线程”这一抽象落实到了具体的数据结构与页表操作上。页表部分的关键不在于写出多少行代码，而在于理解“定位页表项 → 建立/撤销映射 → 维护引用计数 → 失效 TLB”这条链路的每个环节：`get_pte()` 负责定位与中间级页表分配，`page_insert()` 负责让虚拟地址真正指向物理页，引用计数决定物理页何时可以释放，TLB 失效保证硬件不会继续使用旧翻译。任何一个环节缺失，都会出现“页表看起来对、实际访问却错”的问题。

进程部分则把 PCB、内核栈、trapframe 和 context 串成了完整的生命周期：`alloc_proc()` 只负责干净地创建对象，`kernel_thread()` 负责构造“新线程第一次运行”的现场模板，`do_fork()` 负责把资源组织成可运行实体，`copy_thread()` 负责把首次运行链路接到 `forkret → __trapret → kernel_thread_entry`。idleproc 的“直接复用当前执行现场”和 initproc 的“通过 kernel_thread 复制模板”两种创建路径，也让我更清楚地理解了内核线程与普通 fork 的区别。

### AI 协作开发的经验

本次实验说明，向 AI 描述底层内核任务时，“以工程真实代码为准、不要臆造结构布局和接口”是最重要的约束。通过先阅读 `pmm.h`、`proc.h`、`trap.h`、`entry.S`、`switch.S`，再让 AI 按真实接口实现，才能得到可用结果。同时，AI 生成的代码仍需人工核对：例如 `page_ref_dec` 是否自动释放、`copy_thread` 的 `tf == NULL` 保护、idleproc 是否应挂入链表等细节，都需要结合工程实际和实验要求判断。

