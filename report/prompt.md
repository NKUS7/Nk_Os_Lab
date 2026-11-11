### 功能模块一

# [PROMPT]

**任务**：修改文件 `kern/mm/pmm.c`，实现 `page_insert` 与 `page_remove` 两个函数。`page_insert` 负责在现有多级页表框架上建立或更新“线性地址 → 物理页”的映射，并同步维护物理页引用计数、页表项权限位与 TLB 状态；`page_remove` 负责安全撤销已建立的映射，作为幂等删除接口，同时正确递减引用计数并失效 TLB。二者共同决定一个线性地址最终对应哪一页物理内存、物理页引用计数是否正确、页表状态是否一致、TLB 是否需要失效。

**操作要求**：你必须在项目中修改实际文件 `kern/mm/pmm.c`，保留文件中的现有代码，只在 `page_insert`、`page_remove` 及其附近位置进行修改和补充。不要删除既有逻辑，不要修改无关文件，不要修改 `trapentry.S`、`swap.c` 等无关模块。必须先阅读并严格依据工程中已有的 `get_pte`、`pte_create`、`pte2page`、`page_ref_inc`、`page_ref_dec`、`set_page_ref`、`alloc_page`、`free_page`、`tlb_invalidate`、`flush_tlb`、`struct Page`、`struct Page *pages`、`boot_pgdir_va`、`PDE/PTE` 类型定义以及 `PTE_V/PTE_U/PTE_W/PTE_R` 等宏进行实现；不得自行假设与实际页表布局、权限位或引用计数语义不一致的行为。不得修改上述已有接口的签名、语义和 `struct Page` 的定义。实现方式不限定为某一种固定代码序列，但必须保证“定位页表项 → 建立/更新或删除映射 → 维护引用计数 → 失效 TLB → 返回状态”的闭环一致。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 部分提供的信息，以工程真实代码为准，不要引入无关功能。

---

# [RELY]

### 1. 相关文件与已有函数接口

`kern/mm/pmm.c` 中已提供或需要完善的函数/接口包括：

```c
pde_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);  // 定位 la 对应的页表项，create=1 时可能分配中间级页表页
pte_t pte_create(uintptr_t pa, uint32_t perm);             // 造页表项
struct Page *pte2page(pte_t pte);                          // 由页表项取物理页
void page_ref_inc(struct Page *page);                      // 引用计数加一
void page_ref_dec(struct Page *page);                      // 引用计数减一（可能触发释放）
void set_page_ref(struct Page *page, int val);             // 已直接设置引用计数
struct Page *alloc_page(struct page_queue *queue);         // 分配物理页
void free_page(struct Page *page);                         // 释放物理页
void tlb_invalidate(pde_t *pgdir, uintptr_t la);           // 失效单个地址的 TLB
void flush_tlb(void);                                      // 刷新整个 TLB

int  page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);  // 需要实现
void page_remove(pde_t *pgdir, uintptr_t la);                                    // 需要实现
```

### 2. C 语言接口与数据结构

```c
struct Page;                 // 物理页描述符，含 ref 引用计数字段，决定页的分配/释放语义
pde_t;                       // 页目录项类型
pte_t;                       // 页表项类型
struct Page *pages;          // 全局物理页数组
pde_t *boot_pgdir_va;        // 内核页目录的虚拟地址

#define PTE_V  ...           // 有效位
#define PTE_U  ...           // 用户可访问位
#define PTE_W  ...           // 可写位
#define PTE_R  ...           // 可读位
```

- `get_pte(pgdir, la, create)` 是定位目标页表项的唯一入口；`create=1` 时若中间级页表页不存在会触发分配，**可能失败**。
- `page_insert` 与 `page_remove` 不得绕过 `get_pte` 自行遍历页表。
- 引用计数语义由 `page_ref_inc` / `page_ref_dec` / `set_page_ref` 共同保证，不得直接写 `page->ref` 字段绕过接口。

### 3. 内存管理与页表约定

- **权限位**：最终页表项权限必须体现调用者传入的 `perm`，并保证 `PTE_V` 有效位被正确设置；用户页还需按调用者要求体现 `PTE_U`。
- **引用计数**：一个物理页被一个有效映射引用时，其引用计数至少为 1；建立新映射必须 `page_ref_inc`，删除映射必须 `page_ref_dec`。
- **TLB 一致性**：任何对页表项的写入（新建、更新、清空）之后，必须对相应线性地址执行 `tlb_invalidate(pgdir, la)`，否则硬件可能仍使用旧翻译结果。
- **错误处理**：`get_pte(..., 1)` 失败时不得留下半完成映射（例如中间级页表已分配但目标页表项未正确建立，或引用计数已被增加却未回滚）。
- **线性地址对齐**：`la` 应以页为单位使用；实现中按工程既有约定处理页内偏移。

---

# [GUARANTEE]

必须实现或完善以下接口：

```c
int  page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

可以添加：
- 函数内部的局部变量、注释
- 与现有页表项布局、权限位、引用计数语义一致的辅助代码片段

但不得：
- 修改 `get_pte`、`pte_create`、`pte2page`、`page_ref_inc`、`page_ref_dec`、`set_page_ref`、`tlb_invalidate`、`flush_tlb` 的签名与语义
- 修改 `struct Page`、`pages`、`boot_pgdir_va` 的定义
- 修改 `PTE_V/PTE_U/PTE_W/PTE_R` 的位定义
- 绕过 `get_pte` 直接手写页表遍历
- 直接写 `page->ref` 字段绕过引用计数接口
- 引入与“映射建立/撤销”无关的功能

---

# [SPECIFICATION]

## page_insert

**Pre-Condition**：
- `pgdir` 为有效页目录基址（虚拟地址）。
- `page` 为一个有效物理页描述符。
- `la` 为待建立映射的线性地址。
- `perm` 为调用者要求的权限位组合。
- `get_pte`、`pte_create`、`page_ref_inc`、`tlb_invalidate` 等接口可用。

**Post-Condition**：
- 成功时，`la` 稳定映射到 `page` 对应物理页，页表项权限为 `perm`（并保证 `PTE_V` 有效）。
- 物理页引用计数被正确维护：新建立映射时 `page` 的引用计数增加；若原地址已映射到同一物理页，引用计数不重复增加，仅更新权限。
- 若原地址已映射到其他物理页，旧物理页的引用计数被正确递减（必要时释放），新物理页引用计数增加。
- 对 `la` 执行了 TLB 失效。
- 失败时返回 `-E_NO_MEM`，不得留下半完成映射，不得造成引用计数泄漏或页表状态不一致。

**Case 1**：新建立映射（原地址未映射）
- 触发条件：`get_pte(pgdir, la, 1)` 成功且原页表项无效。
- 执行行为：构造新页表项写入，`page_ref_inc(page)`，失效 TLB，返回 0。

**Case 2**：原地址已映射到同一物理页
- 触发条件：原页表项有效且 `pte2page(*ptep) == page`。
- 执行行为：仅更新页表项权限为 `perm`（保持有效位），不重复增加引用计数，失效 TLB，返回 0。

**Case 3**：原地址已映射到其他物理页
- 触发条件：原页表项有效且 `pte2page(*ptep) != page`。
- 执行行为：对新页 `page_ref_inc(page)`，写新页表项，对旧页 `page_ref_dec(old_page)`（按工程约定必要时释放），失效 TLB，返回 0。注意操作顺序应保证失败路径干净。

**Case 4**：中间级页表分配失败
- 触发条件：`get_pte(pgdir, la, 1)` 返回 `NULL`。
- 执行行为：直接返回 `-E_NO_MEM`，不得修改引用计数、不得写入任何页表项、不得留下半完成状态。

**Case 5**：不得绕过接口
- 触发条件：实现过程中涉及页表项定位、页表项构造、引用计数、TLB 失效。
- 执行行为：必须使用 `get_pte`、`pte_create`、`page_ref_inc`、`page_ref_dec`、`tlb_invalidate` 等既有接口；不得手写页表遍历、不得直接写 `page->ref`、不得省略 TLB 失效。

---

## page_remove

**Pre-Condition**：
- `pgdir` 为有效页目录基址。
- `la` 为待撤销映射的线性地址。
- `get_pte`、`page_ref_dec`、`tlb_invalidate` 等接口可用。

**Post-Condition**：
- 若 `la` 存在有效映射：页表项被清空（置无效），对应物理页引用计数被正确递减（必要时释放），对 `la` 执行 TLB 失效。
- 若 `la` 不存在有效映射（包括中间级页表不存在、页表项无效）：安静返回，不修改任何状态，不报错、不触发引用计数变化。
- 作为幂等接口，重复调用不产生副作用。

**Case 1**：目标地址存在有效映射
- 触发条件：`get_pte(pgdir, la, 0)` 返回非 `NULL` 且 `*ptep` 有效。
- 执行行为：取出对应物理页，清空页表项，`page_ref_dec(page)`，失效 TLB，返回。

**Case 2**：中间级页表不存在
- 触发条件：`get_pte(pgdir, la, 0)` 返回 `NULL`。
- 执行行为：直接返回，不做任何修改。

**Case 3**：页表项无效（未映射）
- 触发条件：`get_pte(pgdir, la, 0)` 返回非 `NULL` 但 `*ptep` 无效。
- 执行行为：直接返回，不做任何修改。

**Case 4**：幂等性
- 触发条件：对同一 `la` 多次调用 `page_remove`。
- 执行行为：第一次删除后，后续调用走 Case 2 或 Case 3，安静返回，不重复递减引用计数、不破坏页表状态。

**Case 5**：不得假定一定存在映射
- 触发条件：实现 `page_remove` 时。
- 执行行为：不得假定 `get_pte(..., 0)` 一定非 `NULL`，不得假定 `*ptep` 一定有效；必须显式判空、判有效后再操作。

**Case 6**：不得修改既有接口
- 触发条件：实现 `page_insert` 与 `page_remove` 时涉及 `get_pte`、`pte_create`、`pte2page`、引用计数接口、TLB 接口、`struct Page`、权限位宏。
- 执行行为：保持上述接口签名、语义、数据结构定义和权限位定义不变，只补全映射建立/撤销逻辑本身；不引入与“页表项内容、引用计数、TLB 状态”三者同步无关的功能。

---

## 功能模块二

# [PROMPT]

**任务**：修改文件 `kern/process/proc.c`，实现 `alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm` 六个函数。本模块负责把系统从“已经能运行内核代码”推进到“已经能创建并登记多个内核线程”：`alloc_proc` 得到一个干净的进程控制块；`proc_init` 把当前执行现场包装成 `idleproc` 并创建第一个真正的内核线程 `initproc`；`kernel_thread` 把函数入口和参数整理成线程首次运行所需的临时上下文；`do_fork` 把上下文、内核栈、PID、链表、状态位等资源组合成一个可运行实体；`copy_thread` 把 trapframe 和 context 安放到新线程自己的内核栈上；`copy_mm` 在当前实验阶段承担地址空间处理接口的占位作用。完成后系统中至少存在 `idleproc` 与 `initproc` 两个关键线程，且 `initproc` 能在后续调度中被真正切换执行。

**操作要求**：你必须在项目中修改实际文件 `kern/process/proc.c`，保留文件中的现有代码，只在 `alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm` 及其附近位置进行修改和补充。不要删除既有逻辑，不要修改无关文件，**不得修改 `switch.S`、`trapentry.S`、`trap.c`、`pmm.c` 等无关模块**。必须先阅读并严格依据工程中已有的 `struct proc_struct`、`struct context`、`struct trapframe`、`list_entry_t`、`proc_list`、`hash_list`、`idleproc`、`initproc`、`current`、`nr_process`、`bootstack`、`boot_pgdir_pa`、`kernel_thread_entry`、`forkret`、`forkrets`、`SSTATUS_SPP/SPIE/SIE` 等实际定义与符号进行实现；不得自行假设与实际结构布局、字段偏移、上下文保存顺序或符号名不一致的行为。不得修改上述已有结构定义、全局变量语义、汇编入口符号与已有函数的签名。实现方式不限定为某一种固定代码序列，但必须保证“PCB 初始化 → 资源分配（内核栈/PID）→ 上下文与 trapframe 构造 → 链表挂接 → 状态置就绪 → 返回或失败清理”的闭环一致，并保证 `idleproc`、`initproc`、`current` 三者关系正确。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 部分提供的信息，以工程真实代码为准，不要引入无关功能。

---

# [RELY]

### 1. 相关文件与已有函数/符号接口

`kern/process/proc.c` 中已提供或需要完善的函数/接口包括：

```c
static struct proc_struct *alloc_proc(void);                                    // 需要实现
void proc_init(void);                                                           // 需要实现
int  kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);         // 需要实现
int  do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);      // 需要实现
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf); // 需要实现
static int  copy_mm(uint32_t clone_flags, struct proc_struct *proc);           // 需要实现

struct proc_struct *find_proc(int pid);          // 已提供
void set_proc_name(struct proc_struct *proc, const char *name); // 已提供
void wakeup_proc(struct proc_struct *proc);      // 已提供

void *kmalloc(size_t size);                      // 已提供
void  kfree(void *ptr);                          // 已提供
struct Page *alloc_pages(int n);                 // 已提供
void  free_pages(struct Page *base, int n);      // 已提供
void *page2kva(struct Page *page);               // 已提供
struct Page *kva2page(void *kva);                // 已提供

void list_init(list_entry_t *elm);               // 已提供
void list_add(list_entry_t *listelm, list_entry_t *elm); // 已提供

int  get_pid(void);                              // 已提供

/* 汇编入口符号（已存在，不得修改） */
extern void kernel_thread_entry(void);
extern void forkret(void);
extern void forkrets(void);
extern char bootstack[];                         // 启动阶段内核栈
extern pde_t *boot_pgdir_pa;                     // 内核页目录物理地址
```

### 2. C 语言接口与数据结构

```c
struct proc_struct;     // PCB，含 state / pid / kstack / need_resched / parent / mm /
                        // context / tf / pgdir / flags / name / list_link / hash_link
struct context;         // 14 个 callee-saved 现场：ra, sp, s0~s11
struct trapframe;       // 完整陷入现场，由 SAVE_ALL/copy_thread 决定布局
list_entry_t;           // 双向链表节点
```

全局变量：

```c
struct proc_struct *current;               // 当前运行线程
struct proc_struct *idleproc;              // 第 0 个内核线程
struct proc_struct *initproc;              // 第 1 个内核线程
list_entry_t proc_list;                    // 全部 PCB 的双向链表
list_entry_t hash_list[HASH_LIST_SIZE];    // 按 pid 的哈希表
int nr_process;                            // 进程计数
```

进程状态：

```c
PROC_UNINIT      // 刚创建、未初始化
PROC_SLEEPING    // 阻塞
PROC_RUNNABLE    // 就绪/运行
PROC_ZOMBIE      // 僵尸
```

### 3. 进程管理约定

- **PCB 生命周期**：`alloc_proc` 返回的 PCB 处于 `PROC_UNINIT`，其余字段须处于安全空白值（如 `pid=-1`、`mm=NULL`、`tf=NULL`、`parent=NULL`、`list_link/hash_link` 未挂接）。
- **idleproc 特殊语义**：不是普通 fork 产物，而是**直接复用当前内核执行现场**；其 PID、`kstack`、`pgdir`、`need_resched`、`current` 关系均有特殊含义。
- **initproc 创建路径**：`proc_init` 中通过 `kernel_thread(init_main, ...)` 创建，走 `kernel_thread → do_fork → copy_thread` 路径。
- **首次运行链路**：`copy_thread` 必须与 `forkret`、`forkrets`、`__trapret`、`kernel_thread_entry` 配合，使新线程首次运行能落到 `fn(arg)`。
- **失败清理**：`do_fork` 前 3 步（`alloc_proc`、内核栈分配、`copy_mm`）任一失败，必须释放已占资源，不得挂入链表，不得递增 `nr_process`。
- **状态与挂接顺序**：PCB 必须完成全部字段与上下文构造后才加入 `proc_list`/`hash_list`，随后置 `PROC_RUNNABLE`；`current` 仅在调度切换到该线程时更新。
- **copy_mm 占位语义**：当前实验创建的是内核线程，`current->mm == NULL`，`copy_mm` 不做真正的用户地址空间复制，但**必须保持函数形态与返回语义完整**（成功返回 0），以便后续实验扩展。

---

# [GUARANTEE]

必须实现或完善以下接口（签名不得修改）：

```c
static struct proc_struct *alloc_proc(void);
void proc_init(void);
int  kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int  do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int  copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

可以添加：
- 函数内部的局部变量、注释
- 与现有结构布局、上下文构造、链表/哈希挂接语义一致的辅助代码片段

但不得：
- 修改 `struct proc_struct`、`struct context`、`struct trapframe` 的定义
- 修改 `proc_list`、`hash_list`、`idleproc`、`initproc`、`current`、`nr_process` 的语义
- 修改 `kernel_thread_entry`、`forkret`、`forkrets`、`__trapret` 等汇编符号与调用约定
- 修改 `switch.S`、`trapentry.S`、`trap.c`、`pmm.c`
- 绕过 `alloc_proc` 自行 `kmalloc` 造 PCB、绕过 `get_pid` 自行赋 PID、绕过 `list_add` 直接操作链表指针
- 引入与“内核线程创建、登记、首次运行准备”无关的功能

---

# [SPECIFICATION]

## alloc_proc

**Pre-Condition**：
- `kmalloc`、`memset` 可用；`struct proc_struct` 定义可见。

**Post-Condition**：
- 成功时返回一块已完成基础字段初始化的 PCB：`state = PROC_UNINIT`、`pid = -1`、`kstack = 0`、`need_resched = 0`、`parent = NULL`、`mm = NULL`、`context` 清零、`tf = NULL`、`pgdir = boot_pgdir_pa`、`flags = 0`、`name` 置空、`list_link/hash_link` 处于未挂接的安全状态。
- 失败时返回 `NULL`，不留半初始化全局状态。

**Case 1**：正常分配
- 触发条件：`kmalloc` 成功。
- 执行行为：清零并按上述字段初始化，返回 PCB 指针。

**Case 2**：分配失败
- 触发条件：`kmalloc` 返回 `NULL`。
- 执行行为：返回 `NULL`，不得改动任何全局结构。

**Case 3**：不得留下危险字段
- 触发条件：PCB 返回给调用者之后。
- 执行行为：不得出现“字段未初始化但状态已是可运行”的情况；未初始化状态必须是 `PROC_UNINIT`。

---

## proc_init

**Pre-Condition**：
- `alloc_proc` 可用；全局 `proc_list`、`hash_list` 尚未初始化；`current` 尚未指向任何 PCB。

**Post-Condition**：
- `proc_list`、`hash_list` 已初始化。
- `idleproc` 已指向一个 PCB：PID 有效、状态 `PROC_RUNNABLE`、`kstack = bootstack`、`pgdir = boot_pgdir_pa`、`need_resched = 1`，已挂入 `proc_list`/`hash_list`，`nr_process` 正确递增。
- `current` 指向 `idleproc`。
- 通过 `kernel_thread(init_main, ...)` 成功创建 `initproc`（PID 有效、状态 `PROC_RUNNABLE`、已挂入链表），`initproc` 全局指针被正确赋值。
- 调用结束后，系统处于可被 `cpu_idle → schedule` 调度的状态。

**Case 1**：idleproc 构建
- 触发条件：`proc_init` 进入。
- 执行行为：分配 PCB，设 `pid = 0`、`state = PROC_RUNNABLE`、`kstack = bootstack`、`pgdir = boot_pgdir_pa`、`need_resched = 1`，挂入链表并置 `current = idleproc`。

**Case 2**：initproc 构建
- 触发条件：idleproc 构建完成。
- 执行行为：调用 `kernel_thread(init_main, NULL, 0)`，将其返回值关联到 `initproc`（按工程实际实现方式），并保证其已处于可调度状态。

**Case 3**：不得把 idleproc 当普通 fork
- 触发条件：初始化 idleproc。
- 执行行为：不得走 `do_fork` 路径伪造 idleproc；必须直接复用当前执行现场并赋予特殊字段。

---

## kernel_thread

**Pre-Condition**：
- `fn` 非空；`alloc_proc`、`do_fork` 可用；`read_csr(sstatus)` 与 `SSTATUS_SPP/SPIE/SIE` 宏可见。

**Post-Condition**：
- 在局部构造一份临时 `trapframe` 作为“新线程初始现场模板”，其中：
  - `tf.epc = kernel_thread_entry`（sret 返回后落到入口桩）；
  - `tf.s0 = fn`（入口桩中 `jalr s0` 跳去执行的目标函数）；
  - `tf.s1 = arg`（入口桩中 `move a0, s1` 变成函数参数）；
  - `tf.a0 = 0`（标记子进程）；
  - `tf.status` 按 `read_csr(sstatus)` 读取后置 `SSTATUS_SPP`、`SSTATUS_SPIE`，清 `SSTATUS_SIE`；
  - 其余寄存器按工程约定清零/初始化。
- 以该模板调用 `do_fork(clone_flags, stack, &tf)`，并返回其结果（新线程 PID 或负错误码）。

**Case 1**：正常创建
- 触发条件：`do_fork` 返回非负 PID。
- 执行行为：返回该 PID。

**Case 2**：do_fork 失败
- 触发条件：`do_fork` 返回负错误码。
- 执行行为：原样向上返回该错误码，不做资源回收（由 `do_fork` 负责）。

**Case 3**：不得臆造 trapframe 布局
- 触发条件：构造 `tf`。
- 执行行为：字段命名与布局必须与 `struct trapframe` 实际定义一致；`tf.status` 的位操作必须使用 `SSTATUS_*` 宏。

---

## do_fork

**Pre-Condition**：
- `alloc_proc`、内核栈分配接口、`copy_mm`、`copy_thread`、`get_pid`、`list_add`、`wakeup_proc` 等接口可用。
- `tf` 可为调用者提供的模板（可为 `NULL`，按工程约定处理）。

**Post-Condition**：
- 成功时返回新线程 PID；新 PCB 已完成字段、内核栈、中断帧与上下文构造，已挂入 `proc_list`/`hash_list`，状态为 `PROC_RUNNABLE`，`nr_process` 已递增。
- 失败时返回负错误码，且已占用资源被正确释放，不残留链表节点、不残留 PID 计数、不残留内核栈。

**Case 1**：正常创建（按 7 步）
- 触发条件：全部步骤成功。
- 执行行为：依次执行：① `alloc_proc` 分配 PCB；② 分配并初始化内核栈（`setup_kstack` 语义）；③ `copy_mm(clone_flags, proc)`；④ `copy_thread(proc, stack, tf)`；⑤ 分配 PID 并挂入 `proc_list`/`hash_list`；⑥ 置 `PROC_RUNNABLE`；⑦ 返回 PID。`nr_process` 在挂入链表处递增。

**Case 2**：前 3 步失败清理
- 触发条件：`alloc_proc` 失败、内核栈分配失败、`copy_mm` 失败之一。
- 执行行为：按已占用资源逆序释放（内核栈 → PCB），不挂链表、不分配 PID、不递增 `nr_process`，返回相应负错误码。

**Case 3**：copy_thread 之后失败
- 触发条件：PID 分配或链表挂接阶段出现异常（按工程约定处理）。
- 执行行为：释放内核栈与 PCB，不残留半完成节点，返回负错误码。

**Case 4**：状态与挂接顺序
- 触发条件：正常创建路径。
- 执行行为：必须先完成上下文与 trapframe 构造，再挂链表；挂链表后再置 `PROC_RUNNABLE`；不得提前把 `PROC_UNINIT` 的 PCB 暴露给调度器。

---

## copy_thread

**Pre-Condition**：
- `proc` 已分配且内核栈已就绪；`esp` 为内核栈上可用的栈顶；`tf` 为调用者提供的模板（可为 `NULL`）。
- `proc->kstack` 指向的内核栈空间足够容纳一个 `struct trapframe`。

**Post-Condition**：
- 在 `proc` 的内核栈上分配一块 `struct trapframe` 空间，并将模板内容（或按 `NULL` 约定的默认值）复制/构造到该位置。
- `proc->tf` 指向该 trapframe。
- `proc->context.ra = forkret`；`proc->context.sp` 指向该 trapframe（按工程约定放置）。
- 其余 `context` 字段按工程约定初始化（如清零或按模板设置）。
- 保证新线程首次被 `switch_to` 切换后能经 `forkret → __trapret → kernel_thread_entry → fn(arg)` 正确运行。

**Case 1**：模板非空
- 触发条件：`tf != NULL`。
- 执行行为：把模板内容复制到内核栈上的 trapframe，并按“子进程”语义设置 `tf.a0 = 0` 等定制字段（如工程约定）。

**Case 2**：模板为空
- 触发条件：`tf == NULL`（如 idleproc 相关或工程约定场景）。
- 执行行为：按工程约定清零/初始化 trapframe；不得引用空指针。

**Case 3**：不得臆造 context 布局
- 触发条件：设置 `context`。
- 执行行为：`context.ra` 必须为 `forkret`；`context.sp` 必须指向新 trapframe；字段命名与 `struct context` 实际定义一致。

---

## copy_mm

**Pre-Condition**：
- `proc` 已分配；`current->mm` 在本实验阶段为 `NULL`。

**Post-Condition**：
- 当前实验阶段：不做真正的用户地址空间复制，`proc->mm` 保持 `NULL`（或按工程约定），返回 0 表示成功。
- 保持函数形态完整，便于后续实验扩展为真正的 mm 复制/共享。

**Case 1**：内核线程场景
- 触发条件：`current->mm == NULL`。
- 执行行为：直接返回 0，不分配、不复制 mm。

**Case 2**：后续扩展占位
- 触发条件：实现本函数时。
- 执行行为：保留 `clone_flags` 参数与返回语义；不得删除参数、不得改变返回值约定；不得引入与当前实验无关的 mm 复制逻辑。

**Case 3**：不得修改既有接口
- 触发条件：实现上述六个函数时涉及 `struct proc_struct`、`struct context`、`struct trapframe`、`kernel_thread_entry`、`forkret`、`forkrets`、`switch_to` 等。
- 执行行为：保持上述结构、符号、签名与调用约定不变，只补全本模块要求的 PCB 初始化、线程创建、首次运行准备与占位逻辑；不引入与“内核线程创建/登记/首次运行”无关的功能。

---