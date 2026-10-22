
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
#define KERNBASE 0xFFFFFFFFC0000000 // 内核高地址虚拟空间起始基址
#define PPN(pa) ((pa) >> 12) // 物理地址转物理页号
#define PADDR(kva) ({ uintptr_t __m_kva = (uintptr_t)(kva); if (__m_kva < KERNBASE) { panic("PADDR called with invalid kva"); } __m_kva - va_pa_offset; }) // 内核虚拟地址转物理地址

// 2. 数据结构
struct Page {
    // 框架自带的 Page 结构，重点使用以下相关宏/函数：
    // SetPageReserved(p), ClearPageReserved(p), PageReserved(p)
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
