#include <buddy_pmm.h>
#include <list.h>
#include <pmm.h>
#include <stdio.h>

// A free_area at index N contains aligned blocks of (1 << N) pages.
#define BUDDY_MAX_ORDER 63
#define BUDDY_MAX_RANGES 16

struct buddy_range {
    ppn_t begin;
    ppn_t end;
};

static free_area_t buddy_free_area[BUDDY_MAX_ORDER + 1];
static struct buddy_range buddy_ranges[BUDDY_MAX_RANGES];
static size_t buddy_range_count;
static size_t buddy_total_free_pages;
static unsigned buddy_max_order;

static size_t
buddy_order_size(unsigned order) {
    assert(order <= BUDDY_MAX_ORDER);
    return (size_t)1 << order;
}

static unsigned
buddy_order_of(size_t size) {
    assert(size > 0);
    assert((size & (size - 1)) == 0);

    unsigned order = 0;
    while (size > 1) {
        size >>= 1;
        order++;
    }
    return order;
}

static size_t
buddy_round_up_power_of_two(size_t n) {
    assert(n > 0);

    size_t size = 1;
    while (size < n) {
        if (size > ((size_t)-1) / 2) {
            panic("buddy_alloc_pages: request is too large");
        }
        size <<= 1;
    }
    return size;
}

static size_t
buddy_floor_power_of_two(size_t n) {
    assert(n > 0);

    size_t size = 1;
    while (size <= n / 2) {
        size <<= 1;
    }
    return size;
}

static bool
buddy_range_overlaps(ppn_t begin, ppn_t end) {
    for (size_t i = 0; i < buddy_range_count; i++) {
        if (begin < buddy_ranges[i].end && buddy_ranges[i].begin < end) {
            return 1;
        }
    }
    return 0;
}

static bool
buddy_range_contains(ppn_t begin, ppn_t end) {
    for (size_t i = 0; i < buddy_range_count; i++) {
        if (begin >= buddy_ranges[i].begin && end <= buddy_ranges[i].end) {
            return 1;
        }
    }
    return 0;
}

// Keep every order's free list ordered by absolute physical page number.
static void
buddy_insert_free_block(struct Page *base, unsigned order) {
    assert(base != NULL);
    assert(PageProperty(base));
    assert(base->property == buddy_order_size(order));

    free_area_t *area = &buddy_free_area[order];
    list_entry_t *le = &area->free_list;
    ppn_t base_ppn = page2ppn(base);
    while ((le = list_next(le)) != &area->free_list) {
        struct Page *page = le2page(le, page_link);
        if (base_ppn < page2ppn(page)) {
            list_add_before(le, &base->page_link);
            area->nr_free += buddy_order_size(order);
            return;
        }
    }
    list_add_before(&area->free_list, &base->page_link);
    area->nr_free += buddy_order_size(order);
}

static void
buddy_remove_free_block(struct Page *base, unsigned order) {
    assert(base != NULL);
    free_area_t *area = &buddy_free_area[order];
    size_t size = buddy_order_size(order);
    assert(area->nr_free >= size);

    list_del(&base->page_link);
    area->nr_free -= size;
}

static void
buddy_init(void) {
    for (unsigned order = 0; order <= BUDDY_MAX_ORDER; order++) {
        list_init(&buddy_free_area[order].free_list);
        buddy_free_area[order].nr_free = 0;
    }

    buddy_range_count = 0;
    buddy_total_free_pages = 0;

    size_t managed_pages = npage > nbase ? npage - nbase : 0;
    buddy_max_order = 0;
    while (buddy_max_order < BUDDY_MAX_ORDER &&
           managed_pages / 2 >= buddy_order_size(buddy_max_order)) {
        buddy_max_order++;
    }
    if (managed_pages == 0) {
        buddy_max_order = 0;
    }
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(base != NULL);
    assert(n > 0);

    ppn_t begin = page2ppn(base);
    ppn_t end = begin + n;
    assert(end > begin);
    assert(begin >= nbase);
    assert(end <= npage);
    assert(!buddy_range_overlaps(begin, end));
    if (buddy_range_count >= BUDDY_MAX_RANGES) {
        panic("buddy_init_memmap: too many managed ranges");
    }

    buddy_ranges[buddy_range_count].begin = begin;
    buddy_ranges[buddy_range_count].end = end;
    buddy_range_count++;

    // The caller hands us a contiguous arena, but it need not be a power of
    // two in size or aligned to any block larger than one page.
    for (size_t i = 0; i < n; i++) {
        struct Page *page = base + i;
        page->flags = 0;
        page->property = 0;
        set_page_ref(page, 0);
    }

    ppn_t current = begin;
    while (current < end) {
        size_t remaining = end - current;
        size_t block_pages = buddy_floor_power_of_two(remaining);

        // Limit the block to the largest power-of-two boundary containing
        // current. This decomposes an arbitrary aligned range exactly.
        size_t alignment = current & (~current + 1);
        if (alignment != 0 && alignment < block_pages) {
            block_pages = alignment;
        }
        if (block_pages > buddy_order_size(buddy_max_order)) {
            block_pages = buddy_order_size(buddy_max_order);
        }

        unsigned order = buddy_order_of(block_pages);
        struct Page *page = &pages[current - nbase];
        page->flags = 0;
        page->property = block_pages;
        SetPageProperty(page);
        set_page_ref(page, 0);
        buddy_insert_free_block(page, order);
        buddy_total_free_pages += block_pages;
        current += block_pages;
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);

    size_t wanted = buddy_round_up_power_of_two(n);
    if (wanted > buddy_total_free_pages) {
        return NULL;
    }
    unsigned wanted_order = buddy_order_of(wanted);
    if (wanted_order > buddy_max_order) {
        return NULL;
    }

    unsigned found_order = wanted_order;
    while (found_order <= buddy_max_order &&
           list_empty(&buddy_free_area[found_order].free_list)) {
        found_order++;
    }
    if (found_order > buddy_max_order) {
        return NULL;
    }

    list_entry_t *le = list_next(&buddy_free_area[found_order].free_list);
    struct Page *page = le2page(le, page_link);
    buddy_remove_free_block(page, found_order);

    while (found_order > wanted_order) {
        found_order--;
        size_t half = buddy_order_size(found_order);
        struct Page *remaining = page + half;

        remaining->flags = 0;
        remaining->property = half;
        SetPageProperty(remaining);
        set_page_ref(remaining, 0);
        buddy_insert_free_block(remaining, found_order);
    }

    page->flags = 0;
    page->property = wanted;
    set_page_ref(page, 0);
    buddy_total_free_pages -= wanted;
    return page;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(base != NULL);
    assert(n > 0);
    assert((n & (n - 1)) == 0);
    assert(base->property == n);
    assert(!PageProperty(base));

    ppn_t block_ppn = page2ppn(base);
    unsigned order = buddy_order_of(n);
    assert(order <= buddy_max_order);
    assert(buddy_range_contains(block_ppn, block_ppn + n));
    assert(!PageReserved(base));

    base->flags = 0;
    base->property = n;
    set_page_ref(base, 0);

    while (order < buddy_max_order) {
        size_t block_pages = buddy_order_size(order);
        ppn_t buddy_ppn = block_ppn ^ block_pages;

        if (!buddy_range_contains(buddy_ppn, buddy_ppn + block_pages)) {
            break;
        }

        struct Page *buddy = &pages[buddy_ppn - nbase];
        if (!PageProperty(buddy) || buddy->property != block_pages) {
            break;
        }

        buddy_remove_free_block(buddy, order);
        assert(buddy_total_free_pages >= block_pages);
        buddy_total_free_pages -= block_pages;
        buddy->flags = 0;
        buddy->property = 0;
        set_page_ref(buddy, 0);

        if (buddy_ppn < block_ppn) {
            // The old released head becomes a non-head inside the merged
            // block, so its block-size metadata must not survive.
            base->flags = 0;
            base->property = 0;
            set_page_ref(base, 0);
            block_ppn = buddy_ppn;
            base = buddy;
        }
        order++;
        base->flags = 0;
        base->property = buddy_order_size(order);
        set_page_ref(base, 0);
    }

    base->property = buddy_order_size(order);
    SetPageProperty(base);
    buddy_insert_free_block(base, order);
    buddy_total_free_pages += buddy_order_size(order);
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_total_free_pages;
}

static void
buddy_check(void) {
    size_t total = 0;

    for (unsigned order = 0; order <= buddy_max_order; order++) {
        size_t size = buddy_order_size(order);
        free_area_t *area = &buddy_free_area[order];
        list_entry_t *le = &area->free_list;
        size_t order_total = 0;
        ppn_t previous_ppn = 0;
        bool first_block = 1;

        while ((le = list_next(le)) != &area->free_list) {
            struct Page *page = le2page(le, page_link);
            ppn_t ppn = page2ppn(page);

            assert(PageProperty(page));
            assert(page->property == size);
            assert((ppn & (size - 1)) == 0);
            assert(buddy_range_contains(ppn, ppn + size));
            assert(!PageReserved(page));
            if (!first_block) {
                assert(previous_ppn < ppn);
            }
            previous_ppn = ppn;
            first_block = 0;

            for (size_t i = 1; i < size; i++) {
                assert(!PageProperty(page + i));
                assert(!PageReserved(page + i));
                assert(page[i].property == 0);
            }
            order_total += size;
        }
        assert(order_total == area->nr_free);
        total += order_total;
        assert(total <= buddy_total_free_pages);
    }
    assert(total == buddy_total_free_pages);

    // Check cross-order overlap as well as per-order ordering. The free-block
    // count is small in normal buddy operation, so a quadratic scan is fine.
    for (unsigned first_order = 0;
         first_order <= buddy_max_order;
         first_order++) {
        list_entry_t *first = &buddy_free_area[first_order].free_list;
        while ((first = list_next(first)) !=
               &buddy_free_area[first_order].free_list) {
            struct Page *first_page = le2page(first, page_link);
            ppn_t first_begin = page2ppn(first_page);
            ppn_t first_end = first_begin + buddy_order_size(first_order);

            for (unsigned second_order = first_order;
                 second_order <= buddy_max_order;
                 second_order++) {
                list_entry_t *second =
                    &buddy_free_area[second_order].free_list;
                while ((second = list_next(second)) !=
                       &buddy_free_area[second_order].free_list) {
                    struct Page *second_page = le2page(second, page_link);
                    if (second_page == first_page) {
                        continue;
                    }
                    ppn_t second_begin = page2ppn(second_page);
                    ppn_t second_end =
                        second_begin + buddy_order_size(second_order);
                    assert(first_end <= second_begin ||
                           second_end <= first_begin);
                }
            }
        }
    }
}

#define BUDDY_TEST_ARENA_PAGES 67
#define BUDDY_TEST_REQUEST_COUNT 4

static size_t
buddy_test_expected_size(size_t n) {
    size_t size = 1;
    while (size < n) {
        size <<= 1;
    }
    return size;
}

static void
buddy_test_validate_allocation(struct Page *base, size_t request,
                               struct Page *arena, size_t arena_pages) {
    assert(base != NULL);

    size_t expected = buddy_test_expected_size(request);
    assert(base->property == expected);
    assert(!PageProperty(base));
    assert(!PageReserved(base));
    assert(base >= arena);
    assert(base + expected <= arena + arena_pages);

    ppn_t ppn = page2ppn(base);
    assert((ppn & (expected - 1)) == 0);
    for (size_t i = 0; i < expected; i++) {
        assert(!PageProperty(base + i));
        assert(!PageReserved(base + i));
        assert(page_ref(base + i) == 0);
        if (i != 0) {
            assert((base + i)->property == 0);
        }
    }
}

static bool
buddy_test_blocks_overlap(struct Page *first, size_t first_pages,
                          struct Page *second, size_t second_pages) {
    ppn_t first_begin = page2ppn(first);
    ppn_t first_end = first_begin + first_pages;
    ppn_t second_begin = page2ppn(second);
    ppn_t second_end = second_begin + second_pages;
    return first_begin < second_end && second_begin < first_end;
}

static size_t
buddy_test_fill_arena(const struct pmm_manager *manager,
                      struct Page *arena, struct Page **allocated) {
    size_t count = 0;
    struct Page *page;

    while ((page = manager->alloc_pages(1)) != NULL) {
        assert(count < BUDDY_TEST_ARENA_PAGES);
        buddy_test_validate_allocation(page, 1, arena,
                                       BUDDY_TEST_ARENA_PAGES);
        for (size_t i = 0; i < count; i++) {
            assert(!buddy_test_blocks_overlap(page, 1, allocated[i], 1));
        }
        allocated[count++] = page;
    }

    assert(count == BUDDY_TEST_ARENA_PAGES);
    assert(manager->nr_free_pages() == 0);
    assert(manager->alloc_pages(1) == NULL);
    assert(manager->alloc_pages(2) == NULL);
    assert(manager->nr_free_pages() == 0);
    manager->check();
    return count;
}

static void
buddy_test_cleanup_arena(struct Page *arena, size_t arena_pages) {
    for (size_t i = 0; i < arena_pages; i++) {
        arena[i].flags = 0;
        arena[i].property = 0;
        set_page_ref(&arena[i], 0);
    }
}

void
buddy_pmm_selftest(void) {
    const struct pmm_manager *saved_manager = pmm_manager;
    assert(saved_manager != NULL);

    size_t saved_free_pages = saved_manager->nr_free_pages();
    struct Page *arena =
        saved_manager->alloc_pages(BUDDY_TEST_ARENA_PAGES);
    if (arena == NULL) {
        panic("buddy selftest: cannot reserve a contiguous test arena");
    }
    assert(saved_manager->nr_free_pages() + BUDDY_TEST_ARENA_PAGES ==
           saved_free_pages);

    pmm_manager = &buddy_pmm_manager;
    const struct pmm_manager *buddy = pmm_manager;
    buddy->init();
    buddy->init_memmap(arena, BUDDY_TEST_ARENA_PAGES);
    assert(buddy->nr_free_pages() == BUDDY_TEST_ARENA_PAGES);
    buddy->check();

    // Exercise exact matches and non-power-of-two requests. The latter are
    // rounded up by Buddy and the returned block records the actual size.
    size_t requests[BUDDY_TEST_REQUEST_COUNT] = {1, 2, 3, 5};
    struct Page *blocks[BUDDY_TEST_REQUEST_COUNT];
    size_t block_pages[BUDDY_TEST_REQUEST_COUNT];

    for (size_t i = 0; i < BUDDY_TEST_REQUEST_COUNT; i++) {
        size_t free_before = buddy->nr_free_pages();
        struct Page *page = buddy->alloc_pages(requests[i]);
        buddy_test_validate_allocation(page, requests[i], arena,
                                       BUDDY_TEST_ARENA_PAGES);
        block_pages[i] = page->property;
        for (size_t j = 0; j < i; j++) {
            assert(!buddy_test_blocks_overlap(page, block_pages[i],
                                              blocks[j], block_pages[j]));
        }
        blocks[i] = page;
        assert(buddy->nr_free_pages() + block_pages[i] == free_before);
    }

    for (size_t i = 0; i < BUDDY_TEST_REQUEST_COUNT; i++) {
        buddy->free_pages(blocks[i], block_pages[i]);
    }
    assert(buddy->nr_free_pages() == BUDDY_TEST_ARENA_PAGES);
    buddy->check();

    // A 32-page request is always satisfiable inside 67 consecutive pages.
    // Freeing it must allow the same allocation to succeed again.
    struct Page *large = buddy->alloc_pages(32);
    buddy_test_validate_allocation(large, 32, arena,
                                   BUDDY_TEST_ARENA_PAGES);
    assert(large->property == 32);
    buddy->free_pages(large, 32);
    assert(buddy->nr_free_pages() == BUDDY_TEST_ARENA_PAGES);
    buddy->check();

    struct Page *allocated[BUDDY_TEST_ARENA_PAGES];

    // Fill every free page through single-page requests. This repeatedly
    // splits larger blocks down to order zero.
    size_t count = buddy_test_fill_arena(buddy, arena, allocated);

    // Free the interleaved blocks, then verify complete coalescing.
    for (size_t i = 0; i < count; i += 2) {
        buddy->free_pages(allocated[i], 1);
    }
    for (size_t i = 1; i < count; i += 2) {
        buddy->free_pages(allocated[i], 1);
    }
    assert(buddy->nr_free_pages() == BUDDY_TEST_ARENA_PAGES);
    buddy->check();

    // Repeat exhaustion and release in reverse order.
    count = buddy_test_fill_arena(buddy, arena, allocated);
    for (size_t i = count; i > 0; i--) {
        buddy->free_pages(allocated[i - 1], 1);
    }
    assert(buddy->nr_free_pages() == BUDDY_TEST_ARENA_PAGES);
    buddy->check();

    // Leave the Buddy arena fully allocated so no free-list node points into
    // the pages that will be handed back to Best-Fit.
    count = buddy_test_fill_arena(buddy, arena, allocated);
    assert(count == BUDDY_TEST_ARENA_PAGES);
    buddy->check();

    buddy_test_cleanup_arena(arena, BUDDY_TEST_ARENA_PAGES);
    pmm_manager = saved_manager;
    saved_manager->free_pages(arena, BUDDY_TEST_ARENA_PAGES);
    assert(saved_manager->nr_free_pages() == saved_free_pages);

    cprintf("[Buddy PMM] PASS\n");
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
