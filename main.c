#include "nand.h"
#include "ftl.h"
#include <assert.h>
#include <stdio.h>

#define RUN_TEST(test_func)                  \
    do {                                     \
        printf("[ RUN  ] %s\n", #test_func); \
        test_func();                         \
        printf("[ PASS ] %s\n", #test_func); \
    } while (0)

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

static void test_program_and_read(void)
{
    NANDFlash nand;
    nand_init(&nand);
    uint32_t value = 0;

    assert(nand_program(&nand, 0, 0, 1234) == NAND_OK); // // Writing 1234 to this address, and return NAND_OK
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


static void test_ftl_init(void)
{
    FTL ftl;
    ftl_init(&ftl);
    assert(ftl.next_free_ppn == 0);
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

static void test_ftl_reject_overwrite(void)
{
    FTL ftl;
    ftl_init(&ftl);
    uint32_t value = 0;

    assert(ftl_write(&ftl, 2, 111) == FTL_OK);
    assert(ftl_write(&ftl, 2, 222) == FTL_ERROR_ALREADY_WRITTEN);
    assert(ftl.next_free_ppn == 1);
    assert(ftl.l2p[2] == 0);
    assert(ftl_read(&ftl, 2, &value) == FTL_OK);
    assert(value == 111);
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
    assert(ftl.next_free_ppn == FTL_LOGICAL_PAGE_COUNT);
    assert(ftl_write(&ftl, 0, 9999) == FTL_ERROR_ALREADY_WRITTEN);
    assert(ftl_read(&ftl, 11, &value) == FTL_OK);
    assert(value == 1011);
}

static void test_ftl_nand_failure(void)
{
    FTL ftl;
    ftl_init(&ftl);

    /* Simulate a page already programmed outside the FTL. */
    assert(nand_program(&ftl.nand, 0, 0, 42) == NAND_OK);
    assert(ftl_write(&ftl, 0, 100) == FTL_ERROR_NAND);
    assert(ftl.l2p[0] == FTL_INVALID_PPN);
    assert(ftl.next_free_ppn == 0);
}

int main(void)
{
    RUN_TEST(test_init);
    RUN_TEST(test_program_and_read);
    RUN_TEST(test_program_twice_rejected);
    RUN_TEST(test_read_erased_page);
    RUN_TEST(test_block_erase);
    RUN_TEST(test_invalid_address);
    RUN_TEST(test_null_pointer);

    RUN_TEST(test_ftl_init);
    RUN_TEST(test_ftl_write_and_read);
    RUN_TEST(test_ftl_cross_block);
    RUN_TEST(test_ftl_reject_overwrite);
    RUN_TEST(test_ftl_unmapped_and_invalid);
    RUN_TEST(test_ftl_null_pointer);
    RUN_TEST(test_ftl_full);
    RUN_TEST(test_ftl_nand_failure);

    NANDFlash demo;
    nand_init(&demo);
    assert(nand_program(&demo, 0, 0, 100) == NAND_OK);
    assert(nand_program(&demo, 0, 1, 200) == NAND_OK);
    nand_print(&demo);

    printf("\nAll Stage 1 and Stage 2 tests passed!\n");
    return 0;
}