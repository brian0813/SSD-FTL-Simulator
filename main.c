#include "nand.h"
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

int main(void)
{
    RUN_TEST(test_init);
    RUN_TEST(test_program_and_read);
    RUN_TEST(test_program_twice_rejected);
    RUN_TEST(test_read_erased_page);
    RUN_TEST(test_block_erase);
    RUN_TEST(test_invalid_address);
    RUN_TEST(test_null_pointer);

    NANDFlash demo;
    nand_init(&demo);
    assert(nand_program(&demo, 0, 0, 100) == NAND_OK);
    assert(nand_program(&demo, 0, 1, 200) == NAND_OK);
    nand_print(&demo);

    printf("\nAll Stage 1 tests passed!\n");
    return 0;
}