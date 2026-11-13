#include "nand.h"
#include "ftl.h"
#include <assert.h>
#include <stdio.h>

#define RUN_TEST(test_func)                  \
    do {                                     \
        test_func();                         \
        printf("[ PASS ] %s\n", #test_func); \
    } while (0) /* printf("[ RUN  ] %s\n", #test_func); \ */

static void test_init(void)
{
    NANDFlash nand;
    nand_init(&nand);

    for (int b = 0; b < NAND_BLOCK_COUNT; b++) {
        assert(nand.blocks[b].erase_count == 0);
        for (int p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
            assert(nand.blocks[b].pages[p].state == NAND_PAGE_ERASED);
            assert(nand.blocks[b].pages[p].data == NAND_ERASED_DATA);
        }
    }
}

static void seed_full_nand_layout(FTL *ftl, int mode)
{
    ftl_init(ftl);

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        int ppn = lpn;
        int block = ppn / NAND_PAGES_PER_BLOCK;
        int page = ppn % NAND_PAGES_PER_BLOCK;

        assert(nand_program(&ftl->nand, block, page,
                            (uint32_t)(1000 + lpn)) == NAND_OK);

        ftl->l2p[lpn] = ppn;
    }

    for (int i = 0; i < FTL_RESERVED_PAGE_COUNT; i++) {
        int lpn = (mode == 0) ? i : (i < 2 ? i : i + 2);
        int old_ppn = ftl->l2p[lpn];
        int new_ppn = FTL_LOGICAL_PAGE_COUNT + i;

        assert(nand_program(&ftl->nand,
                            new_ppn / NAND_PAGES_PER_BLOCK,
                            new_ppn % NAND_PAGES_PER_BLOCK,
                            (uint32_t)(2000 + lpn)) == NAND_OK);

        assert(nand_invalidate(&ftl->nand,
                               old_ppn / NAND_PAGES_PER_BLOCK,
                               old_ppn % NAND_PAGES_PER_BLOCK) == NAND_OK);

        ftl->l2p[lpn] = new_ppn;
    }

    ftl->next_free_ppn = 0;
}

static void test_program_and_read(void)
{
    NANDFlash nand;
    nand_init(&nand);
    uint32_t value = 0;

    assert(nand_program(&nand, 0, 0, 1234) == NAND_OK);
    assert(nand_program(&nand, 2, 3, 5678) == NAND_OK);
    assert(nand_read(&nand, 0, 0, &value) == NAND_OK);
    assert(value == 1234);
    assert(nand_read(&nand, 2, 3, &value) == NAND_OK);
    assert(value == 5678);
}

static void test_program_twice_rejected(void)
{
    NANDFlash nand;
    nand_init(&nand);
    uint32_t value = 0;

    assert(nand_program(&nand, 1, 2, 111) == NAND_OK);
    assert(nand_program(&nand, 1, 2, 222)
           == NAND_ERROR_PAGE_ALREADY_PROGRAMMED);
    assert(nand_read(&nand, 1, 2, &value) == NAND_OK);
    assert(value == 111);
}

static void test_read_erased_page(void)
{
    NANDFlash nand;
    nand_init(&nand);
    uint32_t value = 999;

    assert(nand_read(&nand, 0, 0, &value)
           == NAND_ERROR_PAGE_NOT_PROGRAMMED);
    assert(value == 999);
}

static void test_block_erase(void)
{
    NANDFlash nand;
    nand_init(&nand);
    uint32_t value = 0;

    assert(nand_program(&nand, 0, 0, 100) == NAND_OK);
    assert(nand_program(&nand, 0, 1, 200) == NAND_OK);
    assert(nand_program(&nand, 1, 0, 300) == NAND_OK);

    assert(nand_erase(&nand, 0) == NAND_OK);
    assert(nand.blocks[0].erase_count == 1);
    for (int p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
        assert(nand.blocks[0].pages[p].state == NAND_PAGE_ERASED);
        assert(nand.blocks[0].pages[p].data == NAND_ERASED_DATA);
    }
    assert(nand_read(&nand, 0, 0, &value)
           == NAND_ERROR_PAGE_NOT_PROGRAMMED);
    assert(nand_read(&nand, 1, 0, &value) == NAND_OK);
    assert(value == 300);

    assert(nand_program(&nand, 0, 0, 400) == NAND_OK);
    assert(nand_erase(&nand, 0) == NAND_OK);
    assert(nand.blocks[0].erase_count == 2);
}

static void test_invalid_address(void)
{
    NANDFlash nand;
    nand_init(&nand);
    uint32_t value = 0;

    assert(nand_program(&nand, -1, 0, 10)
           == NAND_ERROR_INVALID_ADDRESS);
    assert(nand_program(&nand, NAND_BLOCK_COUNT, 0, 10)
           == NAND_ERROR_INVALID_ADDRESS);
    assert(nand_program(&nand, 0, -1, 10)
           == NAND_ERROR_INVALID_ADDRESS);
    assert(nand_program(&nand, 0, NAND_PAGES_PER_BLOCK, 10)
           == NAND_ERROR_INVALID_ADDRESS);
    assert(nand_read(&nand, NAND_BLOCK_COUNT, 0, &value)
           == NAND_ERROR_INVALID_ADDRESS);
    assert(nand_read(&nand, 0, NAND_PAGES_PER_BLOCK, &value)
           == NAND_ERROR_INVALID_ADDRESS);
    assert(nand_erase(&nand, -1)
           == NAND_ERROR_INVALID_ADDRESS);
    assert(nand_erase(&nand, NAND_BLOCK_COUNT)
           == NAND_ERROR_INVALID_ADDRESS);
}

static void test_null_pointer(void)
{
    NANDFlash nand;
    nand_init(&nand);
    nand_init(NULL);
    nand_print(NULL);

    assert(nand_program(NULL, 0, 0, 10)
           == NAND_ERROR_NULL_POINTER);
    assert(nand_read(NULL, 0, 0, NULL)
           == NAND_ERROR_NULL_POINTER);
    assert(nand_read(&nand, 0, 0, NULL)
           == NAND_ERROR_NULL_POINTER);
    assert(nand_erase(NULL, 0)
           == NAND_ERROR_NULL_POINTER);
}

static void test_nand_invalidate(void)
{
    NANDFlash nand;
    nand_init(&nand);
    uint32_t value = 999;

    assert(nand_program(&nand, 0, 0, 500) == NAND_OK);
    assert(nand_invalidate(&nand, 0, 0) == NAND_OK);
    assert(nand.blocks[0].pages[0].state == NAND_PAGE_INVALID);
    assert(nand.blocks[0].pages[0].data == 500);
    assert(nand_read(&nand, 0, 0, &value)
           == NAND_ERROR_PAGE_NOT_PROGRAMMED);
    assert(value == 999);
    assert(nand_program(&nand, 0, 0, 800)
           == NAND_ERROR_PAGE_ALREADY_PROGRAMMED);
    assert(nand_invalidate(&nand, 0, 0)
           == NAND_ERROR_PAGE_NOT_VALID);
    assert(nand_erase(&nand, 0) == NAND_OK);
    assert(nand.blocks[0].pages[0].state == NAND_PAGE_ERASED);
    assert(nand_program(&nand, 0, 0, 800) == NAND_OK);
    assert(nand_read(&nand, 0, 0, &value) == NAND_OK);
    assert(value == 800);
    assert(nand_invalidate(&nand, 0, 1)
           == NAND_ERROR_PAGE_NOT_VALID);
    assert(nand_invalidate(&nand, -1, 0)
           == NAND_ERROR_INVALID_ADDRESS);
    assert(nand_invalidate(NULL, 0, 0)
           == NAND_ERROR_NULL_POINTER);
}

static void test_ftl_init(void)
{
    FTL ftl;
    ftl_init(&ftl);
    assert(ftl.next_free_ppn == 0);
    assert(FTL_LOGICAL_PAGE_COUNT == 8);
    assert(FTL_PHYSICAL_PAGE_COUNT == 12);
    for (int i = 0; i < FTL_LOGICAL_PAGE_COUNT; i++) {
        assert(ftl.l2p[i] == FTL_INVALID_PPN);
    }
    for (int b = 0; b < NAND_BLOCK_COUNT; b++) {
        assert(ftl.nand.blocks[b].erase_count == 0);
        for (int p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
            assert(ftl.nand.blocks[b].pages[p].state == NAND_PAGE_ERASED);
            assert(ftl.nand.blocks[b].pages[p].data == NAND_ERASED_DATA);
        }
    }
}

static void test_ftl_write_and_read(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    assert(ftl_write(&ftl, 3, 500) == FTL_OK);
    assert(ftl.l2p[3] == 0);
    assert(ftl.next_free_ppn == 1);
    assert(ftl.nand.blocks[0].pages[0].data == 500);
    assert(ftl_read(&ftl, 3, &value) == FTL_OK);
    assert(value == 500);

    assert(ftl_write(&ftl, 1, 800) == FTL_OK);
    assert(ftl.l2p[1] == 1);
    assert(ftl.next_free_ppn == 2);
    assert(ftl_read(&ftl, 1, &value) == FTL_OK);
    assert(value == 800);
}

static void test_ftl_cross_block(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    for (int lpn = 0; lpn < 6; lpn++) {
        assert(ftl_write(&ftl, lpn, (uint32_t)(100 + lpn)) == FTL_OK);
    }
    assert(ftl.l2p[4] == 4);
    assert(ftl.nand.blocks[1].pages[0].data == 104);
    assert(ftl_read(&ftl, 5, &value) == FTL_OK);
    assert(value == 105);
}

// Replaces the Stage 2 test_ftl_reject_overwrite test.
static void test_ftl_overwrite(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    assert(ftl_write(&ftl, 2, 111) == FTL_OK);
    assert(ftl_write(&ftl, 2, 222) == FTL_OK);
    assert(ftl.next_free_ppn == 2);
    assert(ftl.l2p[2] == 1);
    assert(ftl.nand.blocks[0].pages[0].state == NAND_PAGE_INVALID);
    assert(ftl.nand.blocks[0].pages[0].data == 111);
    assert(ftl.nand.blocks[0].pages[1].state == NAND_PAGE_PROGRAMMED);
    assert(ftl_read(&ftl, 2, &value) == FTL_OK);
    assert(value == 222);
}

static void test_ftl_unmapped_and_invalid(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 999;

    assert(ftl_read(&ftl, 3, &value) == FTL_ERROR_UNMAPPED_LPN);
    assert(value == 999);
    assert(ftl_write(&ftl, -1, 123) == FTL_ERROR_INVALID_LPN);
    assert(ftl_write(&ftl, FTL_LOGICAL_PAGE_COUNT, 123)
           == FTL_ERROR_INVALID_LPN);
    assert(ftl_read(&ftl, -1, &value) == FTL_ERROR_INVALID_LPN);
    assert(ftl_read(&ftl, FTL_LOGICAL_PAGE_COUNT, &value)
           == FTL_ERROR_INVALID_LPN);
    assert(ftl.next_free_ppn == 0);
}

static void test_ftl_null_pointer(void)
{
    FTL ftl;
    ftl_init(&ftl);
    ftl_init(NULL);
    assert(ftl_write(NULL, 0, 100) == FTL_ERROR_NULL_POINTER);
    assert(ftl_read(NULL, 0, NULL) == FTL_ERROR_NULL_POINTER);
    assert(ftl_read(&ftl, 0, NULL) == FTL_ERROR_NULL_POINTER);
}

static void test_ftl_full(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        assert(ftl_write(&ftl, lpn, (uint32_t)(lpn + 1000)) == FTL_OK);
    }

    assert(ftl.next_free_ppn == 8);

    for (int lpn = 0; lpn < FTL_RESERVED_PAGE_COUNT; lpn++) {
        assert(ftl_write(&ftl, lpn, (uint32_t)(lpn + 2000)) == FTL_OK);
    }

    assert(ftl.nand.blocks[0].erase_count > 0);
    assert(ftl_write(&ftl, 0, 9999) == FTL_OK);
    assert(ftl_write(&ftl, 7, 8888) == FTL_OK);

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        assert(ftl_read(&ftl, lpn, &value) == FTL_OK);

        uint32_t expected = (uint32_t)(lpn < FTL_RESERVED_PAGE_COUNT ? 2000 + lpn : 1000 + lpn);

        if (lpn == 0) {
            expected = 9999;
        } else if (lpn == 7) {
            expected = 8888;
        }

        assert(value == expected);
    }
}

static void test_ftl_nand_failure(void)
{
    FTL ftl;
    ftl_init(&ftl);

    // Simulate a page already programmed outside the FTL.
    assert(nand_program(&ftl.nand, 0, 0, 42) == NAND_OK);
    assert(ftl_write(&ftl, 0, 100) == FTL_ERROR_NAND);
    assert(ftl.l2p[0] == FTL_INVALID_PPN);
    assert(ftl.next_free_ppn == 0);
}

static void test_ftl_multiple_updates(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    assert(ftl_write(&ftl, 2, 111) == FTL_OK);  // PPN 0
    assert(ftl_write(&ftl, 2, 222) == FTL_OK);  // PPN 1
    assert(ftl_write(&ftl, 2, 333) == FTL_OK);  // PPN 2
    assert(ftl.l2p[2] == 2);
    assert(ftl.next_free_ppn == 3);
    assert(ftl.nand.blocks[0].pages[0].state == NAND_PAGE_INVALID);
    assert(ftl.nand.blocks[0].pages[1].state == NAND_PAGE_INVALID);
    assert(ftl.nand.blocks[0].pages[2].state == NAND_PAGE_PROGRAMMED);
    assert(ftl_read(&ftl, 2, &value) == FTL_OK);
    assert(value == 333);
}

static void test_ftl_update_cross_block(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    for (int lpn = 0; lpn < 4; lpn++) {
        assert(ftl_write(&ftl, lpn, (uint32_t)(100 + lpn)) == FTL_OK);
    }
    assert(ftl_write(&ftl, 1, 901) == FTL_OK);
    assert(ftl.l2p[1] == 4);
    assert(ftl.next_free_ppn == 5);
    assert(ftl.nand.blocks[0].pages[1].state == NAND_PAGE_INVALID);
    assert(ftl.nand.blocks[1].pages[0].state == NAND_PAGE_PROGRAMMED);
    assert(ftl_read(&ftl, 1, &value) == FTL_OK);
    assert(value == 901);
}

static void test_ftl_failed_update_program(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    assert(ftl_write(&ftl, 3, 500) == FTL_OK);
    assert(nand_program(&ftl.nand, 0, 1, 42) == NAND_OK);
    assert(ftl_write(&ftl, 3, 800) == FTL_ERROR_NAND);
    assert(ftl.l2p[3] == 0);
    assert(ftl.next_free_ppn == 1);
    assert(ftl.nand.blocks[0].pages[0].state == NAND_PAGE_PROGRAMMED);
    assert(ftl_read(&ftl, 3, &value) == FTL_OK);
    assert(value == 500);
}

static void test_ftl_failed_invalidation(void)
{
    FTL ftl;
    ftl_init(&ftl);

    assert(ftl_write(&ftl, 3, 500) == FTL_OK);
    assert(nand_invalidate(&ftl.nand, 0, 0) == NAND_OK);
    assert(ftl_write(&ftl, 3, 800) == FTL_ERROR_NAND);

    assert(ftl.l2p[3] == 0);
    assert(ftl.next_free_ppn == 1);
    assert(ftl.nand.blocks[0].pages[1].state == NAND_PAGE_ERASED);
}

static void test_gc_empty(void)
{
    FTL ftl;
    FTLBlockStats stats;
    int victim = -1;

    ftl_init(&ftl);

    assert(ftl_gc_get_block_stats(&ftl, 0, &stats) == FTL_OK);
    assert(stats.erased_pages == 4);
    assert(stats.valid_pages == 0);
    assert(stats.invalid_pages == 0);

    assert(ftl_gc_select_victim(&ftl, &victim)
           == FTL_ERROR_NO_GC_VICTIM);
    assert(victim == -1);
}

static void test_gc_select_victim(void)
{
    FTL ftl;
    FTLBlockStats stats;
    int victim = -1;

    ftl_init(&ftl);

    assert(ftl_write(&ftl, 0, 100) == FTL_OK);
    assert(ftl_write(&ftl, 1, 200) == FTL_OK);
    assert(ftl_write(&ftl, 0, 300) == FTL_OK);
    assert(ftl_write(&ftl, 2, 400) == FTL_OK);
    assert(ftl_write(&ftl, 3, 500) == FTL_OK);
    assert(ftl_write(&ftl, 3, 600) == FTL_OK);

    assert(ftl_gc_get_block_stats(&ftl, 0, &stats) == FTL_OK);
    assert(stats.erased_pages == 0);
    assert(stats.valid_pages == 3);
    assert(stats.invalid_pages == 1);

    assert(ftl_gc_get_block_stats(&ftl, 1, &stats) == FTL_OK);
    assert(stats.erased_pages == 2);
    assert(stats.valid_pages == 1);
    assert(stats.invalid_pages == 1);

    assert(ftl_gc_select_victim(&ftl, &victim) == FTL_OK);
    assert(victim == 1);
}

static void test_gc_full_invalid_block(void)
{
    FTL ftl;
    int victim = -1;

    seed_full_nand_layout(&ftl, 0); // mode = 0

    assert(ftl_gc_select_victim(&ftl, &victim) == FTL_OK);
    assert(victim == 0);
}

static void test_gc_full_no_migration_space(void)
{
    FTL ftl;
    int victim = -1;

    seed_full_nand_layout(&ftl, 1);

    assert(ftl_gc_select_victim(&ftl, &victim) == FTL_ERROR_NO_GC_VICTIM);
    assert(victim == -1);
}

static void test_gc_invalid_arguments(void)
{
    FTL ftl;
    FTLBlockStats stats;
    int victim = -1;

    ftl_init(&ftl);

    assert(ftl_gc_get_block_stats(NULL, 0, &stats)
           == FTL_ERROR_NULL_POINTER);
    assert(ftl_gc_get_block_stats(&ftl, 0, NULL)
           == FTL_ERROR_NULL_POINTER);
    assert(ftl_gc_get_block_stats(&ftl, -1, &stats)
           == FTL_ERROR_INVALID_BLOCK);
    assert(ftl_gc_get_block_stats(&ftl, NAND_BLOCK_COUNT, &stats)
           == FTL_ERROR_INVALID_BLOCK);

    assert(ftl_gc_select_victim(NULL, &victim)
           == FTL_ERROR_NULL_POINTER);
    assert(ftl_gc_select_victim(&ftl, NULL)
           == FTL_ERROR_NULL_POINTER);
}

static void test_gc_migrate_one_page(void)
{
    FTL ftl;
    ftl_init(&ftl);

    uint32_t value = 0;
    int victim = -1;

    assert(ftl_write(&ftl, 0, 100) == FTL_OK);
    assert(ftl_write(&ftl, 1, 200) == FTL_OK);
    assert(ftl_write(&ftl, 0, 300) == FTL_OK);
    assert(ftl_write(&ftl, 2, 400) == FTL_OK);
    assert(ftl_write(&ftl, 3, 500) == FTL_OK);
    assert(ftl_write(&ftl, 3, 600) == FTL_OK);

    assert(ftl_gc_select_victim(&ftl, &victim) == FTL_OK);
    assert(victim == 1);

    assert(ftl_gc_migrate_valid_pages(&ftl, victim) == FTL_OK);

    assert(ftl.l2p[3] == 8);
    assert(ftl.next_free_ppn == 9);

    assert(ftl.nand.blocks[1].pages[1].state ==
           NAND_PAGE_INVALID);
    assert(ftl.nand.blocks[2].pages[0].state ==
           NAND_PAGE_PROGRAMMED);

    assert(ftl.nand.blocks[1].erase_count == 0);

    assert(ftl_read(&ftl, 3, &value) == FTL_OK);
    assert(value == 600);

    assert(ftl_write(&ftl, 4, 700) == FTL_OK);
    assert(ftl.l2p[4] == 9);
}

static void test_gc_migrate_two_pages(void)
{
    FTL ftl;
    ftl_init(&ftl);

    uint32_t value = 0;

    assert(ftl_write(&ftl, 0, 100) == FTL_OK);
    assert(ftl_write(&ftl, 1, 200) == FTL_OK);
    assert(ftl_write(&ftl, 2, 300) == FTL_OK);
    assert(ftl_write(&ftl, 3, 400) == FTL_OK);
    assert(ftl_write(&ftl, 0, 500) == FTL_OK);
    assert(ftl_write(&ftl, 1, 600) == FTL_OK);

    assert(ftl_gc_migrate_valid_pages(&ftl, 0) == FTL_OK);

    assert(ftl.l2p[2] == 6);
    assert(ftl.l2p[3] == 7);
    assert(ftl.next_free_ppn == 8);

    for (int p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
        assert(ftl.nand.blocks[0].pages[p].state ==
               NAND_PAGE_INVALID);
    }

    assert(ftl_read(&ftl, 0, &value) == FTL_OK);
    assert(value == 500);
    assert(ftl_read(&ftl, 1, &value) == FTL_OK);
    assert(value == 600);
    assert(ftl_read(&ftl, 2, &value) == FTL_OK);
    assert(value == 300);
    assert(ftl_read(&ftl, 3, &value) == FTL_OK);
    assert(value == 400);

    assert(ftl.nand.blocks[0].erase_count == 0);
}

static void test_gc_full_invalid_victim(void)
{
    FTL ftl;
    uint32_t value = 0;

    seed_full_nand_layout(&ftl, 0);

    assert(ftl.next_free_ppn == 0);
    assert(ftl_gc_migrate_valid_pages(&ftl, 0) == FTL_OK);

    for (int p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
        assert(ftl.nand.blocks[0].pages[p].state == NAND_PAGE_INVALID);
    }

    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl.next_free_ppn == 0);

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        assert(ftl_read(&ftl, lpn, &value) == FTL_OK);
        assert(value == (uint32_t)(lpn < FTL_RESERVED_PAGE_COUNT ? 2000 + lpn : 1000 + lpn));
    }
}

static void test_gc_no_space_preflight(void)
{
    FTL ftl;
    int previous_l2p[FTL_LOGICAL_PAGE_COUNT];

    seed_full_nand_layout(&ftl, 1);

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        previous_l2p[lpn] = ftl.l2p[lpn];
    }

    assert(ftl_gc_migrate_valid_pages(&ftl, 0) == FTL_ERROR_NO_FREE_PAGE);

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        assert(ftl.l2p[lpn] == previous_l2p[lpn]);
    }

    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl.nand.blocks[0].pages[2].state == NAND_PAGE_PROGRAMMED);
}

static void test_gc_orphan_and_invalid_args(void)
{
    FTL ftl;
    ftl_init(&ftl);

    assert(ftl_gc_migrate_valid_pages(NULL, 0)
           == FTL_ERROR_NULL_POINTER);
    assert(ftl_gc_migrate_valid_pages(&ftl, -1)
           == FTL_ERROR_INVALID_BLOCK);
    assert(ftl_gc_migrate_valid_pages(&ftl, NAND_BLOCK_COUNT)
           == FTL_ERROR_INVALID_BLOCK);
    assert(ftl_gc_migrate_valid_pages(&ftl, 0)
           == FTL_ERROR_NO_GC_VICTIM);

    // Create an orphan programmed page outside the FTL.
    assert(nand_program(&ftl.nand, 0, 0, 42) == NAND_OK);
    assert(nand_program(&ftl.nand, 0, 1, 123) == NAND_OK);
    assert(nand_invalidate(&ftl.nand, 0, 1) == NAND_OK);

    assert(ftl_gc_migrate_valid_pages(&ftl, 0)
           == FTL_ERROR_NAND);

    assert(ftl.nand.blocks[0].pages[0].state ==
           NAND_PAGE_PROGRAMMED);
    assert(ftl.nand.blocks[0].pages[0].data == 42);
    assert(ftl.next_free_ppn == 0);
}

static void test_gc_erase_after_migration(void)
{
    FTL ftl;
    ftl_init(&ftl);

    int victim = -1;
    uint32_t data = 0;

    assert(ftl_write(&ftl, 0, 100) == FTL_OK);
    assert(ftl_write(&ftl, 1, 200) == FTL_OK);
    assert(ftl_write(&ftl, 0, 300) == FTL_OK);
    assert(ftl_write(&ftl, 2, 400) == FTL_OK);
    assert(ftl_write(&ftl, 3, 500) == FTL_OK);
    assert(ftl_write(&ftl, 3, 600) == FTL_OK);

    // Step 1: Select victim.
    assert(ftl_gc_select_victim(&ftl, &victim) == FTL_OK);
    assert(victim == 1);

    // Step 2: Migrate valid pages.
    assert(ftl_gc_migrate_valid_pages(&ftl, victim) == FTL_OK);
    assert(ftl.l2p[3] == 8);
    assert(ftl.next_free_ppn == 9);

    // Step 3: Erase victim.
    assert(ftl_gc_erase_victim(&ftl, victim) == FTL_OK);
    assert(ftl.nand.blocks[1].erase_count == 1);

    for (int p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
        assert(ftl.nand.blocks[1].pages[p].state ==
               NAND_PAGE_ERASED);

        assert(ftl.nand.blocks[1].pages[p].data ==
               NAND_ERASED_DATA);
    }

    // L2P and allocation cursor must remain unchanged.
    assert(ftl.l2p[3] == 8);
    assert(ftl.next_free_ppn == 9);

    // All logical data must still be readable.
    const uint32_t expected[] = {300, 200, 400, 600};

    for (int lpn = 0; lpn < 4; lpn++) {
        assert(ftl_read(&ftl, lpn, &data) == FTL_OK);
        assert(data == expected[lpn]);
    }

    // Avoid unnecessary repeated erase.
    assert(ftl_gc_erase_victim(&ftl, victim) ==
           FTL_ERROR_NO_GC_VICTIM);

    assert(ftl.nand.blocks[1].erase_count == 1);
}

static void test_gc_erase_reject_valid_page(void)
{
    FTL ftl;
    ftl_init(&ftl);

    uint32_t value = 0;

    assert(ftl_write(&ftl, 0, 100) == FTL_OK);
    assert(ftl_write(&ftl, 0, 200) == FTL_OK);

    assert(ftl_gc_erase_victim(&ftl, 0) ==
           FTL_ERROR_GC_BLOCK_NOT_READY);

    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl.nand.blocks[0].pages[1].state ==
           NAND_PAGE_PROGRAMMED);

    assert(ftl.l2p[0] == 1);
    assert(ftl_read(&ftl, 0, &value) == FTL_OK);
    assert(value == 200);
}

static void test_gc_erase_full_invalid_block(void)
{
    FTL ftl;
    uint32_t value = 0;

    seed_full_nand_layout(&ftl, 0);

    assert(ftl_gc_erase_victim(&ftl, 0) == FTL_OK);
    assert(ftl.nand.blocks[0].erase_count == 1);

    for (int p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
        assert(ftl.nand.blocks[0].pages[p].state == NAND_PAGE_ERASED);
        assert(ftl.nand.blocks[0].pages[p].data == NAND_ERASED_DATA);
    }

    assert(ftl.next_free_ppn == 0);

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        assert(ftl_read(&ftl, lpn, &value) == FTL_OK);
        assert(value == (uint32_t)(lpn < FTL_RESERVED_PAGE_COUNT ? 2000 + lpn : 1000 + lpn));
    }

    // Verify that reclaimed PPN 0 can be reused.
    assert(ftl_write(&ftl, 4, 9999) == FTL_OK);
    assert(ftl.l2p[4] == 0);
    assert(ftl_read(&ftl, 4, &value) == FTL_OK);
    assert(value == 9999);
}

static void test_gc_erase_reject_stale_l2p(void)
{
    FTL ftl;
    ftl_init(&ftl);

    assert(ftl_write(&ftl, 2, 500) == FTL_OK);

    // Intentionally corrupt the NAND state for testing.
    assert(nand_invalidate(&ftl.nand, 0, 0) == NAND_OK);

    // L2P[2] still points to PPN 0.
    assert(ftl_gc_erase_victim(&ftl, 0) ==
           FTL_ERROR_GC_BLOCK_NOT_READY);

    assert(ftl.l2p[2] == 0);
    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl.nand.blocks[0].pages[0].state ==
           NAND_PAGE_INVALID);
}

static void test_gc_erase_invalid_args(void)
{
    FTL ftl;
    ftl_init(&ftl);

    assert(ftl_gc_erase_victim(NULL, 0) ==
           FTL_ERROR_NULL_POINTER);

    assert(ftl_gc_erase_victim(&ftl, -1) ==
           FTL_ERROR_INVALID_BLOCK);

    assert(ftl_gc_erase_victim(&ftl, NAND_BLOCK_COUNT) ==
           FTL_ERROR_INVALID_BLOCK);

    // An erased block does not need GC.
    assert(ftl_gc_erase_victim(&ftl, 0) ==
           FTL_ERROR_NO_GC_VICTIM);

    assert(ftl.nand.blocks[0].erase_count == 0);
}

static void test_ftl_gc_auto_trigger(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        assert(ftl_write(&ftl, lpn, (uint32_t)(1000 + lpn)) == FTL_OK);
    }

    assert(ftl_write(&ftl, 0, 2000) == FTL_OK);
    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl.l2p[0] == 8);

    // Only 3 erased pages remain. The next write triggers GC.
    assert(ftl_write(&ftl, 1, 2001) == FTL_OK);

    assert(ftl.nand.blocks[0].erase_count == 1);
    assert(ftl.l2p[0] == 8);
    assert(ftl.l2p[1] == 0);
    assert(ftl.l2p[2] == 10);
    assert(ftl.l2p[3] == 11);
    assert(ftl.next_free_ppn == 1);

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        assert(ftl_read(&ftl, lpn, &value) == FTL_OK);
        assert(value == (uint32_t)(lpn == 0 ? 2000 : lpn == 1 ? 2001 : 1000 + lpn));
    }
}

static void test_ftl_gc_wraparound(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    // Position the cursor at the last physical page.
    ftl.next_free_ppn = FTL_PHYSICAL_PAGE_COUNT - 1;

    assert(ftl_write(&ftl, 0, 100) == FTL_OK);
    assert(ftl.l2p[0] == 11);
    assert(ftl.next_free_ppn == 0);

    assert(ftl_write(&ftl, 1, 200) == FTL_OK);
    assert(ftl.l2p[1] == 0);
    assert(ftl.next_free_ppn == 1);

    assert(ftl_read(&ftl, 0, &value) == FTL_OK);
    assert(value == 100);

    assert(ftl_read(&ftl, 1, &value) == FTL_OK);
    assert(value == 200);
}

static void test_ftl_gc_manual_collect(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    assert(ftl_gc_collect(NULL) == FTL_ERROR_NULL_POINTER);
    assert(ftl_gc_collect(&ftl) == FTL_ERROR_NO_GC_VICTIM);

    assert(ftl_write(&ftl, 0, 100) == FTL_OK);
    assert(ftl_write(&ftl, 0, 200) == FTL_OK);

    assert(ftl_gc_collect(&ftl) == FTL_OK);

    assert(ftl.nand.blocks[0].erase_count == 1);
    assert(ftl.l2p[0] == 4);

    assert(ftl_read(&ftl, 0, &value) == FTL_OK);
    assert(value == 200);

    // Test allocation from reclaimed PPN 0.
    ftl.next_free_ppn = 0;

    assert(ftl_write(&ftl, 1, 300) == FTL_OK);
    assert(ftl.l2p[1] == 0);

    assert(ftl_read(&ftl, 1, &value) == FTL_OK);
    assert(value == 300);
}

static void test_ftl_gc_full_no_victim(void)
{
    FTL ftl;
    uint32_t value = 0;

    seed_full_nand_layout(&ftl, 1);

    assert(ftl_gc_collect(&ftl) == FTL_ERROR_NO_GC_VICTIM);
    assert(ftl_write(&ftl, 0, 9999) == FTL_ERROR_NO_FREE_PAGE);

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        assert(ftl_read(&ftl, lpn, &value) == FTL_OK);

        uint32_t expected = (uint32_t)((lpn == 0 || lpn == 1 || lpn == 4 || lpn == 5) ? 2000 + lpn : 1000 + lpn);

        assert(value == expected);
    }
}

static void test_ftl_gc_detect_corrupted_mapping(void)
{
    FTL ftl;
    ftl_init(&ftl);

    // Create an orphan programmed page outside the FTL.
    assert(nand_program(&ftl.nand, 0, 0, 42) == NAND_OK);

    assert(ftl_gc_collect(&ftl) == FTL_ERROR_NAND);
    assert(ftl_write(&ftl, 0, 500) == FTL_ERROR_NAND);

    assert(ftl.l2p[0] == FTL_INVALID_PPN);
    assert(ftl.next_free_ppn == 0);
    assert(ftl.nand.blocks[0].pages[0].data == 42);
}

static void test_ftl_gc_repeated_updates(void)
{
    FTL ftl;
    ftl_init(&ftl);

    uint32_t expected[FTL_LOGICAL_PAGE_COUNT];
    uint32_t value = 0;

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        expected[lpn] = (uint32_t)(1000 + lpn);
        assert(ftl_write(&ftl, lpn, expected[lpn]) == FTL_OK);
    }

    for (int i = 0; i < 200; i++) {
        int lpn = (i * 5 + 3) % FTL_LOGICAL_PAGE_COUNT;

        expected[lpn] = (uint32_t)(2000 + i);
        assert(ftl_write(&ftl, lpn, expected[lpn]) == FTL_OK);

        for (int k = 0; k < FTL_LOGICAL_PAGE_COUNT; k++) {
            assert(ftl_read(&ftl, k, &value) == FTL_OK);
            assert(value == expected[k]);
        }
    }

    uint32_t total_erases = 0;

    for (int b = 0; b < NAND_BLOCK_COUNT; b++) {
        total_erases += ftl.nand.blocks[b].erase_count;
    }

    assert(total_erases > 0);
}

int main(void)
{
    // Stage 1: NAND Flash Model
    RUN_TEST(test_init);
    RUN_TEST(test_program_and_read);
    RUN_TEST(test_program_twice_rejected);
    RUN_TEST(test_read_erased_page);
    RUN_TEST(test_block_erase);
    RUN_TEST(test_invalid_address);
    RUN_TEST(test_null_pointer);
    RUN_TEST(test_nand_invalidate);

    // Stage 2: L2P Mapping
    RUN_TEST(test_ftl_init);
    RUN_TEST(test_ftl_write_and_read);
    RUN_TEST(test_ftl_cross_block);
    RUN_TEST(test_ftl_overwrite);
    RUN_TEST(test_ftl_unmapped_and_invalid);
    RUN_TEST(test_ftl_null_pointer);
    RUN_TEST(test_ftl_full);
    RUN_TEST(test_ftl_nand_failure);

    // Stage 3: Out-of-Place Update
    RUN_TEST(test_ftl_multiple_updates);
    RUN_TEST(test_ftl_update_cross_block);
    RUN_TEST(test_ftl_failed_update_program);
    RUN_TEST(test_ftl_failed_invalidation);

    // Stage 4 - Step 1: Victim Selection
    RUN_TEST(test_gc_empty);
    RUN_TEST(test_gc_select_victim);
    RUN_TEST(test_gc_full_invalid_block);
    RUN_TEST(test_gc_full_no_migration_space);
    RUN_TEST(test_gc_invalid_arguments);

    // Stage 4 - Step 2: Valid Page Migration
    RUN_TEST(test_gc_migrate_one_page);
    RUN_TEST(test_gc_migrate_two_pages);
    RUN_TEST(test_gc_full_invalid_victim);
    RUN_TEST(test_gc_no_space_preflight);
    RUN_TEST(test_gc_orphan_and_invalid_args);

    // Stage 4 - Step 3: Safe Block Erase
    RUN_TEST(test_gc_erase_after_migration);
    RUN_TEST(test_gc_erase_reject_valid_page);
    RUN_TEST(test_gc_erase_full_invalid_block);
    RUN_TEST(test_gc_erase_reject_stale_l2p);
    RUN_TEST(test_gc_erase_invalid_args);

    // Stage 4 - Step 4: Free Page Reuse and Automatic GC
    RUN_TEST(test_ftl_gc_auto_trigger);
    RUN_TEST(test_ftl_gc_wraparound);
    RUN_TEST(test_ftl_gc_manual_collect);
    RUN_TEST(test_ftl_gc_full_no_victim);
    RUN_TEST(test_ftl_gc_detect_corrupted_mapping);
    RUN_TEST(test_ftl_gc_repeated_updates);

    printf("\nAll tests through Stage 4 Step 4 passed!\n");
    return 0;
}