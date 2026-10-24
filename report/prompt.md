
---

### 功能模块一：
[PROMPT]
**任务**：修改文件 `kern/mm/pmm.c`，实现或完善 `page_init` 函数。该函数负责从设备树获取物理内存信息，建立 `struct Page` 描述符数组布局，排除 OpenSBI、内核镜像和描述符数组占用的空间后，将剩余的完整空闲物理页交给当前 `pmm_manager` 管理。

**操作要求**：你必须在项目中修改实际文件，保留文件中的现有代码，只在你需要补全的函数 `page_init` 及其附近位置进行修改和补充。不要删除既有逻辑，不要修改无关文件。

**输出要求**：直接进行文件操作，使用下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 部分提供的信息，以工程真实代码为准，不要引入无关功能。

[RELY]

// 1. 物理内存与地址转换相关的宏定义 (参考 memlayout.h / pmm.h)

#define PGSIZE 4096 // 页大小

#define DRAM_BASE 0x80000000 // QEMU DRAM 起始物理地址

#define KERNBASE 0xFFFFFFFFC0000000 // 内核高地址虚拟空间起始
基址

#define PPN(pa) ((pa) >> 12) // 物理地址转物理页号

#define PADDR(kva) ({ uintptr_t __m_kva = (uintptr_t)(kva); if (__m_kva < KERNBASE) { panic("PADDR called with invalid kva"); } __m_kva - va_pa_offset; }) // 内核虚拟地址转物理地址

// 2. 数据结构

struct Page {

    // 框架自带的 Page 结构，重点使用以下相关宏/函数：

    // SetPageReserved(p), ClearPageReserved(p), 
    PageReserved(p)

    // p->property (该连续空闲块的大小)
};

// 3. 可用的全局变量及函数

// 设备树探测结果（由 dtb_init 提前初始化）

uint64_t get_memory_base(void); 

uint64_t get_memory_size(void);

// pmm 框架全局变量

extern struct Page *pages;      // Page 描述符数组的首地址（虚拟地址）

extern size_t npage;            // 内核可管理的物理页号上界 (总物理页数)

extern uint64_t va_pa_offset;   // 虚拟地址与物理地址的偏移量

extern const size_t nbase;      // DRAM_BASE / PGSIZE

// 链接脚本符号
extern char end[];              // 内核代码结尾的虚拟地址

// 可用函数
void panic(const char *fmt, ...); // 报错并终止

int cprintf(const char *fmt, ...); // 内核打印

void init_memmap(struct Page *base, size_t n); // 将连续物理页 base 开始的 n 页交给 pmm_manager 初始化

struct Page *pa2page(uintptr_t pa); // 物理地址转 Page 结构体指针

[GUARANTEE]
必须实现或修改的接口：
```c
static void page_init(void);
```

你可以根据需要添加：
- 辅助函数
- 内部使用的宏定义
- 模块内部的静态变量

[SPECIFICATION]
## page_init
**Pre-Condition**: 
- `pmm_init()` 已被调用。
- `pmm_manager` 已通过 `init_pmm_manager()` 初始化完成。
- DTB 信息已通过 `dtb_init()` 解析，`get_memory_base()` 和 `get_memory_size()` 可获取正确的物理内存起止信息。

**Post-Condition**: 
- 全局变量 `va_pa_offset` 被正确计算。
- 全局变量 `npage` 被正确设置为物理内存末尾对应的页号上界。
- 全局变量 `pages` 指向内核 `end` 之后按页对齐的物理地址（转换为虚拟地址后）。
- 从 `pages` 开始的 `npage - nbase` 个 `struct Page` 被标记为 Reserved（保留状态）。
- 内核镜像、Page 描述符数组已从可用内存中排除。
- 剩余的可用完整物理页面已通过 `init_memmap()` 注册到 `pmm_manager` 中。

  **Case 1**: (设备树信息异常)
  - **触发条件**：`get_memory_size() == 0` 或 `get_memory_base() == 0`，或内存范围小于 `DRAM_BASE` 导致无法管理。
  - **执行行为**：调用 `panic()` 终止初始化。

  **Case 2**: (正常初始化流程)
  - **触发条件**：DTB 信息有效。
  - **执行行为**：
    1. 计算 `va_pa_offset`：基于内核在物理地址 `0x80200000` 启动，而在虚拟地址 `KERNBASE + 0x200000` 运行，推导 `va_pa_offset = KERNEL_BEGIN_VADDR - KERNEL_BEGIN_PADDR`。
    2. 计算 `npage`：通过 `get_memory_base() + get_memory_size()` 得到物理内存末端，将其向下按页对齐，再除以 `PGSIZE` 得到 `npage`。
    3. 初始化 `pages` 数组起点：将内核结束虚拟地址 `end` 向上按页对齐，通过 `PADDR()` 转为物理地址，再转回对应可用的虚拟地址存入 `pages`。
    4. 遍历从 `pa2page(nbase)` 开始到 `npage` 的所有 Page 结构体，调用 `SetPageReserved()` 将其初始状态置为保留。
    5. 计算空闲内存物理起点：`pages` 数组末尾（通过 `PADDR` 转物理地址）向上按页对齐。
    6. 计算空闲内存物理终点：物理内存末端向下按页对齐。
    7. 若起点 < 终点，计算包含的页数 `n`，调用 `init_memmap(pa2page(空闲起点物理地址), n)`。
    8. 通过 `cprintf` 输出探测到的物理内存范围、内核占用的 Page 数组范围以及可用空闲内存范围。
  
  **Case 3**: (无可用空闲页面)
  - **触发条件**：经过对齐和扣除后，空闲物理内存起点 >= 空闲物理内存终点。
  - **执行行为**：**不得**调用 `init_memmap()`。通过 `cprintf()` 打印警告信息，告知没有可用空闲页面。


  #### 模块二任务二最终提示词

````markdown
[PROMPT]
**任务**：修改 `kern/mm/best_fit_pmm.c` 和 `kern/mm/pmm.c`，根据 ucore 现有物理内存管理框架，实现完整的 Best-Fit（最佳适应）连续物理页分配器，并将内核当前使用的物理内存管理器切换为 Best-Fit。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。保留现有代码、注释和测试逻辑，只在必要位置补充或更新实现。不要修改 `best_fit_pmm.c` 中已有的 `basic_check()` 和 `best_fit_check()` 测试函数，也不要修改测试使用的状态约定、测试要求的返回页地址或断言条件。不要修改 `default_pmm.c` 中的 First-Fit 实现，也不要修改文件名、公共接口或 Makefile。

**输出要求**：直接将代码写入相应文件并保证能够编译。`best_fit_pmm.c` 中应包含完整的分配器实现、所需内部状态以及 `best_fit_pmm_manager` 注册；`pmm.c` 的 `init_pmm_manager()` 应实际选择 `best_fit_pmm_manager`。不要只输出代码片段，也不要在实现完成后删除已有测试函数。

[RELY]
// 宏定义与页状态
#define PGSIZE 4096
#define PG_reserved 0
#define PG_property 1

#define PageReserved(page)      (((page)->flags >> PG_reserved) & 1)
#define SetPageReserved(page)   ((page)->flags |= (1UL << PG_reserved))
#define ClearPageReserved(page) ((page)->flags &= ~(1UL << PG_reserved))
#define PageProperty(page)      (((page)->flags >> PG_property) & 1)
#define SetPageProperty(page)   ((page)->flags |= (1UL << PG_property))
#define ClearPageProperty(page) ((page)->flags &= ~(1UL << PG_property))

// 双向链表
typedef struct list_entry {
    struct list_entry *prev;
    struct list_entry *next;
} list_entry_t;

#define le2page(le, member) to_struct((le), struct Page, member)

// 页描述符
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

typedef struct {
    list_entry_t free_list;
    unsigned int nr_free;
} free_area_t;

struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};

extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;

typedef uintptr_t ppn_t;

static inline ppn_t page2ppn(struct Page *page);
static inline uintptr_t page2pa(struct Page *page);
static inline struct Page *pa2page(uintptr_t pa);
static inline int page_ref(struct Page *page);
static inline void set_page_ref(struct Page *page, int val);

void list_init(list_entry_t *list);
bool list_empty(list_entry_t *list);
void list_add(list_entry_t *listelm, list_entry_t *elm);
void list_add_before(list_entry_t *listelm, list_entry_t *elm);
void list_del(list_entry_t *listelm);
list_entry_t *list_next(list_entry_t *listelm);
list_entry_t *list_prev(list_entry_t *listelm);

struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);
#define alloc_page() alloc_pages(1)
#define free_page(page) free_pages(page, 1)

// best_fit_pmm.c 中已有的测试函数会直接引用 free_list 和 nr_free。
// 因现有 best_fit_check() 同时作为管理器的 check 回调，不得另建同名函数。

[GUARANTEE]
必须实现或修改的接口：

```c
extern const struct pmm_manager best_fit_pmm_manager;

static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);

static void init_pmm_manager(void);
```

你可以根据需要添加：

- 辅助函数，辅助函数建议声明为 `static`
- 内部使用的宏定义
- 模块内部的静态变量或空闲区状态

Best-Fit 管理器的 `.check` 应绑定到文件中已有的 `best_fit_check()`。不要重新实现或修改 `basic_check()` 和 `best_fit_check()`。

[SPECIFICATION]

## best_fit_init

**Pre-Condition**：Best-Fit 模块已被选为当前物理内存管理器，且尚未注册任何可分配内存块。

**Post-Condition**：模块的空闲链表被初始化为空链表，空闲物理页总数为 0，不保留其他分配器的内部状态。

## best_fit_init_memmap

**Pre-Condition**：`base` 指向物理连续且长度为 `n` 页的有效描述符区间，`n > 0`，这些页面尚未加入 Best-Fit 空闲链表。

**Post-Condition**：该区间被记录为一个空闲块；首页记录块大小 `n` 并标记为空闲块首页；区间内页描述符的引用计数和占用状态被恢复；空闲链表按物理地址升序插入该块；空闲页总数增加 `n`。

## best_fit_alloc_pages

**Pre-Condition**：`n > 0`，分配器已经初始化，空闲链表中的每个节点均表示一个有效的连续空闲块。

**Post-Condition**：成功时返回连续 `n` 页的首页，并且该块是所有可容纳请求的块中剩余页数最少的一个；相同时选择物理地址最低的块。若块大于请求，则只分配头部 `n` 页，剩余部分保持空闲；分配后空闲页数减少 `n`。失败时返回 `NULL` 且不修改任何状态。

**Case 1：精确匹配**：存在大小恰好为 `n` 的空闲块时，完整移除该块并返回其首页。

**Case 2：需要拆分**：最小的可用候选块大于 `n` 时，从该块头部取出 `n` 页，剩余部分按物理地址顺序重新加入空闲链表。

**Case 3：资源不足**：空闲页总数不足或不存在足够大的块时返回 `NULL`，链表和计数保持不变。

## best_fit_free_pages

**Pre-Condition**：`base` 指向此前分配的连续 `n` 页区间，`n > 0`，这些页不属于保留区域且当前不是空闲块首页。

**Post-Condition**：释放区间重新成为空闲块，首页记录大小 `n`；释放块按物理地址插入链表；若与前后空闲块物理相邻则进行合并；合并后只保留一个有效首页，并清除被吸收块的首页标志；空闲页总数增加 `n`。

**Case 1：两侧均不相邻**：将释放块作为独立节点插入有序链表。

**Case 2：仅一侧相邻**：将释放块与相邻块合并，并由合并后覆盖完整地址范围的首页记录新大小。

**Case 3：两侧均相邻**：将三方合并为一个空闲块，清除中间首页状态，并更新链表节点。

## best_fit_nr_free_pages

**Pre-Condition**：Best-Fit 已完成初始化。

**Post-Condition**：返回所有空闲块页数之和；函数不得修改链表或其他状态。

## init_pmm_manager

**Pre-Condition**：First-Fit 和 Best-Fit 管理器均已编译并链接。

**Post-Condition**：全局 `pmm_manager` 指向 `best_fit_pmm_manager`，并继续执行管理器初始化流程，使 `page_init()` 将初始空闲内存注册给 Best-Fit。

**Requirements**：

- 空闲链表始终按物理地址升序维护。
- 分配或释放后，`Page` 的 `property`、首页标志和空闲页计数必须一致。
- 不允许把保留页、内核页、描述符数组或已分配页重新加入空闲链表。
- `best_fit_pmm_manager` 的六个成员分别绑定初始化、内存映射注册、分配、释放、计数和既有检查函数。
- 同一编译单元必须提供测试函数所引用的 `free_list` 和 `nr_free`。
- 不要求加锁，允许通过遍历全部空闲块完成最佳匹配。
- 不得修改测试函数，也不得针对特定测试地址硬编码。
````

#### Best-Fit 算法是否有进一步的改进空间？

有。当前 Best-Fit 每次分配都需要线性遍历空闲链表，最坏时间复杂度为 \(O(N)\)。可以按空闲块大小建立多级分离链表，或使用红黑树、线段树等数据结构同时维护块大小和物理地址顺序，从而更快地找到最小可用块。也可以引入延迟合并、按处理器维护空闲区等策略降低锁竞争，但这些优化会提高实现复杂度，对于当前实验规模并非必需。


#### 模块二任务三最终提示词（一）：生成 Buddy System 模块并接入内核

````markdown
[PROMPT]
**任务**：在 `kern/mm/` 下新增 `buddy_pmm.c` 和 `buddy_pmm.h`，参考现有 ucore 的 `struct Page`、`free_area_t`、双向链表和 `struct pmm_manager` 框架，从零实现 Buddy System 连续物理页分配器，并将该管理器接入内核的物理内存管理模块。

**操作要求**：你必须在项目中修改或新增实际文件，而不是仅展示代码。保留现有 First-Fit 和 Best-Fit 实现及其测试，不得修改 `default_pmm.c`、`best_fit_pmm.c` 中的现有测试逻辑。保持 `pmm.c` 的 `init_pmm_manager()` 当前仍选择 `best_fit_pmm_manager`，因为后续 Buddy 测试会临时切换管理器，Lab2 最终运行状态仍必须保持 Best-Fit。不要新建 `kern/test` 目录，不要修改 Makefile；新增的 `kern/mm/buddy_pmm.c` 应能被现有构建规则自动编译。

**输出要求**：直接将 Buddy 模块写入 `kern/mm/buddy_pmm.c`，将管理器声明写入 `kern/mm/buddy_pmm.h`，并在 `pmm.c` 中包含该头文件。不要只输出代码片段，不要删除已有代码，也不要修改现有 First-Fit、Best-Fit 的分配语义。

[RELY]
#define PGSIZE 4096
#define PG_reserved 0
#define PG_property 1

#define PageReserved(page)      (((page)->flags >> PG_reserved) & 1)
#define SetPageReserved(page)   ((page)->flags |= (1UL << PG_reserved))
#define ClearPageReserved(page) ((page)->flags &= ~(1UL << PG_reserved))
#define PageProperty(page)      (((page)->flags >> PG_property) & 1)
#define SetPageProperty(page)   ((page)->flags |= (1UL << PG_property))
#define ClearPageProperty(page) ((page)->flags &= ~(1UL << PG_property))

typedef struct list_entry {
    struct list_entry *prev;
    struct list_entry *next;
} list_entry_t;

#define le2page(le, member) to_struct((le), struct Page, member)

struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

typedef struct {
    list_entry_t free_list;
    unsigned int nr_free;
} free_area_t;

struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};

extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;
extern const struct pmm_manager *pmm_manager;

typedef uintptr_t ppn_t;

static inline ppn_t page2ppn(struct Page *page);
static inline uintptr_t page2pa(struct Page *page);
static inline struct Page *pa2page(uintptr_t pa);
static inline int page_ref(struct Page *page);
static inline void set_page_ref(struct Page *page, int val);

void list_init(list_entry_t *list);
bool list_empty(list_entry_t *list);
void list_add(list_entry_t *listelm, list_entry_t *elm);
void list_add_before(list_entry_t *listelm, list_entry_t *elm);
void list_add_after(list_entry_t *listelm, list_entry_t *elm);
void list_del(list_entry_t *listelm);
void list_del_init(list_entry_t *listelm);
list_entry_t *list_next(list_entry_t *listelm);
list_entry_t *list_prev(list_entry_t *listelm);

void panic(const char *fmt, ...);
#define assert(expression) ((void)((expression) || (panic("assertion failed: %s", #expression), 0)))

[GUARANTEE]
必须在 `kern/mm/buddy_pmm.c` 中实现：

```c
extern const struct pmm_manager buddy_pmm_manager;

static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
static void buddy_check(void);
```

必须在 `kern/mm/buddy_pmm.h` 中声明：

```c
extern const struct pmm_manager buddy_pmm_manager;
```

允许添加按阶组织的空闲区数组、最大阶限制、物理页号对齐与伙伴计算辅助函数、空闲块插入移除与拆分合并辅助函数，以及管理器结构检查辅助函数。所有内部辅助函数建议声明为 `static`。

[SPECIFICATION]

## buddy_init

**Pre-Condition**：Buddy 模块已编译并链接，尚未拥有任何可分配物理页。

**Post-Condition**：所有阶的空闲链表为空且计数为 0；模块根据内核可管理物理内存确定内部阶数限制；不得复用其他分配器的空闲状态。

## buddy_init_memmap

**Pre-Condition**：`base` 非空，`n > 0`，指向内核可管理范围内一个物理连续且尚未加入 Buddy 空闲链表的区间；区间起点和长度不要求是高于 1 页的 2 的幂对齐。

**Post-Condition**：区间内每个物理页被纳入 Buddy 且只纳入一次；区间被表示为若干互不重叠、物理连续、自然对齐的 2 的幂空闲块；空闲块首页记录正确大小，各阶链表按物理页号升序，总空闲页数等于 `n`，且没有空闲块跨越输入区间边界。

## buddy_alloc_pages

**Pre-Condition**：`n > 0`，Buddy 已完成初始化，各阶空闲块大小、对齐和计数保持一致。

**Post-Condition**：请求被向上取整为实际块大小 `m`；成功时返回连续、自然对齐的 `m` 页块首页，并从满足要求的最小阶中选取物理页号最低的块；较大块被逐级拆分；返回首页记录实际块大小；总空闲页数减少 `m`；失败时返回 `NULL` 且状态不变。

**Case 1：资源不足**：不存在足够高阶的空闲块时直接返回 `NULL`。

**Case 2：精确匹配**：目标阶存在空闲块时，移除并返回该块。

**Case 3：逐级拆分**：从更高阶块逐层拆出另一半并放入对应阶，最终返回目标阶块。

## buddy_free_pages

**Pre-Condition**：`base` 是此前由 Buddy 分配的块首页，`n` 与实际块大小一致，且不与任何空闲块重叠。

**Post-Condition**：释放块加入对应阶；首页记录正确大小；空闲页总数增加 `n`；若伙伴位于已管理范围内、属于同一阶且空闲，则递归合并为更高阶块；被吸收块的首页状态和块大小被清除；所有链表保持有序和计数一致。

**Case 1：伙伴不可合并**：释放块保留为当前阶空闲块。

**Case 2：伙伴空闲**：两个伙伴合并为高一阶块，并继续尝试向上合并。

**Case 3：伙伴超出管理范围**：不得与范围外页面合并。

## buddy_nr_free_pages

**Pre-Condition**：Buddy 已完成初始化。

**Post-Condition**：返回所有阶空闲块页数之和，且不修改任何状态。

## buddy_check

**Pre-Condition**：Buddy 元数据处于稳定状态，检查过程不得永久改变分配状态。

**Post-Condition**：各阶链表节点均为正确大小和对齐的空闲块首页；不同块互不重叠并位于已管理范围；各阶计数、总计数和实际块页数一致；非首页不得残留空闲块首页状态；失败时通过断言或 `panic()` 终止。

**Requirements**：

- 阶数、对齐和伙伴关系必须根据物理页号计算。
- 初始区间可能不是 2 的幂或高对齐块起点，初始化逻辑必须支持。
- 非 2 的幂请求按实际向上取整后的块大小管理；调用者使用实际块大小释放。
- 每个公开操作完成后，空闲链表和计数应保持可检查的一致状态。
- 当前为单线程启动环境，不要求加锁；允许通过遍历寻找空闲块。
- 不得硬编码 QEMU 的固定内存大小、空闲区起点或测试专用地址。
- `make` 必须成功，且不得影响现有 Best-Fit 的编译、测试和自动评分。
````

#### 模块二任务三最终提示词（二）：设计 Buddy System 测试函数

````markdown
[PROMPT]
**任务**：针对现有 `kern/mm/buddy_pmm.c` 中的 Buddy System 分配器，补充完整的测试函数和测试入口，验证其初始化、非 2 的幂请求、对齐、拆分、伙伴合并、资源耗尽、失败回滚和状态恢复行为。将测试接入内核启动流程，并确保测试结束后恢复原有的 Best-Fit 管理器和空闲内存状态。

**操作要求**：你必须在项目中修改实际文件，而不是仅展示代码。不要修改 `basic_check()`、`best_fit_check()` 或其他已有分配器的测试逻辑，不得通过降低断言或硬编码测试返回值来制造通过。不要新建 `kern/test` 目录，不要修改 Makefile 中的 `KINCLUDE` 或 `KSRCDIR`，以免破坏现有 Lab2 评分测试注入。测试代码应放在现有 `kern/mm` 编译路径中。

**输出要求**：在 Buddy 模块中实际实现测试函数，并通过 `buddy_pmm.h` 暴露必要的测试入口。在 `pmm.c` 中调用测试函数，但测试完成后必须将 `pmm_manager` 恢复为测试前的 Best-Fit 管理器。测试全部通过时应输出明确的 PASS 信息；失败时应通过断言或 `panic()` 终止。最终 `make` 应成功，现有 `make grade` 仍应得到 `page init` 和 `best fit` 全部通过。

[RELY]
extern const struct pmm_manager *pmm_manager;
extern const struct pmm_manager buddy_pmm_manager;

struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};

struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

#define PageReserved(page) (((page)->flags >> 0) & 1)
#define PageProperty(page) (((page)->flags >> 1) & 1)
#define ClearPageProperty(page) ((page)->flags &= ~(1UL << 1))
#define set_page_ref(page, val) ((page)->ref = (val))

static inline ppn_t page2ppn(struct Page *page);
static inline uintptr_t page2pa(struct Page *page);
static inline struct Page *pa2page(uintptr_t pa);

struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);

void panic(const char *fmt, ...);
#define assert(expression) ((void)((expression) || (panic("assertion failed: %s", #expression), 0)))

// pmm_init() 的正常顺序为：
// init_pmm_manager() -> page_init() -> check_alloc_page()
// 测试入口应在这三项完成之后调用。

[GUARANTEE]
必须在 Buddy 模块中实现：

```c
void buddy_pmm_selftest(void);
```

必须在 `buddy_pmm.h` 中声明：

```c
void buddy_pmm_selftest(void);
```

必须在 `pmm.c` 的 Best-Fit 初始化、页描述符注册和自身检查完成后调用测试入口。

允许添加测试辅助函数、测试区域大小常量、连续性检查函数、对齐检查函数和用于恢复状态的辅助函数。

[SPECIFICATION]

## buddy_pmm_selftest

**Pre-Condition**：Best-Fit 已初始化并通过检查；`page_init()` 已注册可用物理页；系统单线程运行；Best-Fit 至少能提供一块足够大的连续测试区。

**Post-Condition**：Buddy 的初始化、内存映射注册、分配、释放、计数和结构检查均被实际调用；测试页最终全部归还给测试前的 Best-Fit；`pmm_manager` 恢复到原来的 Best-Fit；Buddy 链表不保留指向已归还页面的节点；页引用计数、保留状态和首页状态已清理；Best-Fit 空闲页总数恢复；成功时输出明确的 `[Buddy PMM] PASS`。

**Case 1：测试区域申请失败**：输出失败信息并终止，不切换管理器。

**Case 2：Buddy 行为正确**：完成全部测试，清理 Buddy 元数据，恢复 Best-Fit，归还测试区域并输出 PASS。

**Case 3：Buddy 行为错误**：遇到无效地址、块重叠、对齐错误、计数错误、伙伴未合并或资源耗尽后仍返回页面等情况时立即通过断言或 `panic()` 终止。

**测试内容要求**：

1. 从 Best-Fit 申请一个物理连续且页数不是 2 的幂的测试区，切换 Buddy 后注册该区域，验证空闲页数与区间块覆盖。
2. 请求 1、2、3、5 页，验证连续、对齐、向上取整块大小、互不重叠，以及计数按实际块大小变化；释放后计数恢复。
3. 分配较大 2 的幂块验证拆分，释放后验证伙伴合并并可再次分配。
4. 连续进行多次分配，检查两两不重叠，全部释放后检查空闲页数恢复。
5. 反复请求 1 页直至返回 `NULL`，验证空闲页数为 0，失败状态重复请求仍返回 `NULL` 且计数不变。
6. 在初始化后、分配释放后和耗尽状态下调用 `buddy_check()`。
7. 清理测试区所有页面状态，恢复 Best-Fit 并归还区域，验证空闲页计数与测试前一致。

**Requirements**：

- 测试必须通过 `struct pmm_manager` 的公开回调调用 Buddy，不得直接篡改 Buddy 空闲链表或计数来伪造结果。
- 测试区必须来自 Best-Fit 的真实可用物理内存，不得硬编码页号或物理地址。
- 非 2 的幂请求按实际返回块大小释放。
- 资源耗尽测试后必须重新取得全部测试页，或通过等价方式确保页面不同时残留在 Buddy 和 Best-Fit 链表中。
- 最终必须恢复 `best_fit_pmm_manager`。
- 不得修改已有的 First-Fit、Best-Fit 测试函数。
- 不得新建 `kern/test` 或修改 Makefile 中的测试目录配置。
- 测试失败必须立即终止，不能继续启动流程。
- `make` 成功、Buddy 自测输出 PASS、`make grade` 仍保持 `Total Score: 20/20`。
````