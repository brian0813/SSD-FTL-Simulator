#include "ftl.h"
#include "test_support.h"
#include "test_suite.h"

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

void run_ftl_tests(void)
{
    RUN_TEST(test_ftl_init);
    RUN_TEST(test_ftl_write_and_read);
    RUN_TEST(test_ftl_cross_block);
    RUN_TEST(test_ftl_overwrite);
    RUN_TEST(test_ftl_unmapped_and_invalid);
    RUN_TEST(test_ftl_null_pointer);
    RUN_TEST(test_ftl_full);
    RUN_TEST(test_ftl_nand_failure);
    RUN_TEST(test_ftl_multiple_updates);
    RUN_TEST(test_ftl_update_cross_block);
    RUN_TEST(test_ftl_failed_update_program);
    RUN_TEST(test_ftl_failed_invalidation);
}
