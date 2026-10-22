#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void page_init(void);
static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &default_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 YOUR CODE

/* page_init - detect usable physical memory and build the Page descriptor
 * array.  Every physical page is marked reserved first (this covers OpenSBI,
 * the kernel image and the descriptor array itself); the pages that remain
 * free after the array are then handed to the current pmm_manager. */
static void page_init(void) {
    extern char end[];

    // The kernel is linked at KERNEL_BEGIN_VADDR but loaded at
    // KERNEL_BEGIN_PADDR, so the virtual/physical offset is fixed:
    // KERNEL_BEGIN_VADDR - KERNEL_BEGIN_PADDR == PHYSICAL_MEMORY_OFFSET.
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    // physical memory range reported by the device tree
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    if (mem_base == 0 || mem_size == 0 || mem_base < DRAM_BASE) {
        panic("page_init: invalid physical memory range from DTB");
    }

    // npage is the upper bound of the absolute page number.  The memory end
    // is rounded down so that only complete pages are managed.
    uintptr_t mem_end = ROUNDDOWN(mem_base + mem_size, PGSIZE);
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: no physical memory to manage");
    }

    // The Page descriptor array is placed at the first page-aligned address
    // after the kernel image.  Round-trip through PADDR to make sure the
    // array really lives in the physical memory we are managing.
    uintptr_t pages_va = ROUNDUP((uintptr_t)end, PGSIZE);
    uintptr_t pages_pa = PADDR(pages_va);
    pages = (struct Page *)(pages_pa + va_pa_offset);

    // Reserve all descriptors first: kernel text/data, OpenSBI and the
    // descriptor array itself can never be allocated by the pmm_manager.
    size_t nr_pages = npage - nbase;
    for (size_t i = 0; i < nr_pages; i++) {
        pages[i].ref = 0;
        pages[i].flags = 0;
        pages[i].property = 0;
        SetPageReserved(&pages[i]);
    }

    // Free memory begins after the descriptor array and ends at the end of
    // the physical memory, both page aligned.
    uintptr_t free_start =
        ROUNDUP(pages_pa + nr_pages * sizeof(struct Page), PGSIZE);
    uintptr_t free_end = mem_end;

    cprintf("Physical memory: 0x%016lx - 0x%016lx (%lu MB)\n",
            (unsigned long)mem_base, (unsigned long)mem_end,
            (unsigned long)(mem_size / (1024 * 1024)));
    cprintf("Page array:     0x%016lx - 0x%016lx (%lu pages)\n",
            (unsigned long)pages_pa,
            (unsigned long)(pages_pa + nr_pages * sizeof(struct Page)),
            (unsigned long)nr_pages);

    if (free_start < free_end) {
        size_t n = (free_end - free_start) / PGSIZE;
        cprintf("Free memory:    0x%016lx - 0x%016lx (%lu pages)\n",
                (unsigned long)free_start, (unsigned long)free_end,
                (unsigned long)n);
        init_memmap(pa2page(free_start), n);
    } else {
        cprintf("page_init: warning, no free pages available\n");
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
