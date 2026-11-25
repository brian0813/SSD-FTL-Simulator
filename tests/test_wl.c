#include "ftl.h"
#include "test_support.h"
#include "test_suite.h"

static void seed_wl_equal_cost_layout(FTL *ftl)
{
    ftl_init(ftl);

    for (int i = 0; i < 3; i++) {
        assert(nand_erase(&ftl->nand, 0) == NAND_OK);
    }
    assert(nand_erase(&ftl->nand, 2) == NAND_OK);

    assert(nand_program(&ftl->nand, 0, 0, 100) == NAND_OK);
    assert(nand_program(&ftl->nand, 0, 1, 101) == NAND_OK);
    assert(nand_program(&ftl->nand, 1, 0, 200) == NAND_OK);
    assert(nand_program(&ftl->nand, 1, 1, 201) == NAND_OK);
    assert(nand_invalidate(&ftl->nand, 0, 0) == NAND_OK);
    assert(nand_invalidate(&ftl->nand, 1, 0) == NAND_OK);

    ftl->l2p[0] = 1;
    ftl->l2p[1] = 5;
}

static void test_wl_stats_initial(void)
{
    FTL ftl;
    FTLWearStats stats = {0};
    ftl_init(&ftl);

    assert(ftl_wl_get_stats(&ftl, &stats) == FTL_OK);
    assert(stats.min_erase_count == 0);
    assert(stats.max_erase_count == 0);
    assert(stats.wear_gap == 0);
    assert(stats.min_block == 0);
    assert(stats.max_block == 0);
}

static void test_wl_stats_uneven(void)
{
    FTL ftl;
    FTLWearStats stats = {0};
    ftl_init(&ftl);

    assert(nand_erase(&ftl.nand, 0) == NAND_OK);
    assert(nand_erase(&ftl.nand, 0) == NAND_OK);
    assert(nand_erase(&ftl.nand, 2) == NAND_OK);

    assert(ftl_wl_get_stats(&ftl, &stats) == FTL_OK);
    assert(stats.min_erase_count == 0);
    assert(stats.max_erase_count == 2);
    assert(stats.wear_gap == 2);
    assert(stats.min_block == 1);
    assert(stats.max_block == 0);
}

static void test_wl_stats_after_gc(void)
{
    FTL ftl;
    FTLWearStats stats = {0};
    uint32_t value = 0;
    ftl_init(&ftl);

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        assert(ftl_write(&ftl, lpn, (uint32_t)(1000 + lpn)) == FTL_OK);
    }

    assert(ftl_write(&ftl, 0, 2000) == FTL_OK);
    assert(ftl_write(&ftl, 1, 3000) == FTL_OK);
    assert(ftl_wl_get_stats(&ftl, &stats) == FTL_OK);
    assert(stats.min_erase_count == 0);
    assert(stats.max_erase_count == 1);
    assert(stats.wear_gap == 1);
    assert(stats.min_block == 1);
    assert(stats.max_block == 0);

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        uint32_t expected = (uint32_t)(1000 + lpn);
        if (lpn == 0) {
            expected = 2000;
        } else if (lpn == 1) {
            expected = 3000;
        }
        assert(ftl_read(&ftl, lpn, &value) == FTL_OK);
        assert(value == expected);
    }
}

static void test_wl_stats_null_pointer(void)
{
    FTL ftl;
    FTLWearStats stats = {0};
    ftl_init(&ftl);

    assert(ftl_wl_get_stats(NULL, &stats) == FTL_ERROR_NULL_POINTER);
    assert(ftl_wl_get_stats(&ftl, NULL) == FTL_ERROR_NULL_POINTER);
}

static void test_wl_gc_prefers_less_worn_block(void)
{
    FTL ftl;
    int victim_block = -1;
    seed_wl_equal_cost_layout(&ftl);

    assert(ftl.nand.blocks[0].erase_count == 3);
    assert(ftl.nand.blocks[1].erase_count == 0);
    assert(ftl_gc_select_victim(&ftl, &victim_block) == FTL_OK);
    assert(victim_block == 1);
}

static void test_wl_gc_invalid_priority(void)
{
    FTL ftl;
    int victim_block = -1;
    seed_wl_equal_cost_layout(&ftl);

    assert(nand_program(&ftl.nand, 0, 2, 300) == NAND_OK);
    assert(nand_invalidate(&ftl.nand, 0, 2) == NAND_OK);

    assert(ftl_gc_select_victim(&ftl, &victim_block) == FTL_OK);
    assert(victim_block == 0);
}

static void test_wl_gc_valid_priority(void)
{
    FTL ftl;
    int victim_block = -1;
    seed_wl_equal_cost_layout(&ftl);

    assert(nand_program(&ftl.nand, 1, 2, 300) == NAND_OK);
    ftl.l2p[2] = 6;

    assert(ftl_gc_select_victim(&ftl, &victim_block) == FTL_OK);
    assert(victim_block == 0);
}

static void test_wl_gc_equal_wear_keeps_order(void)
{
    FTL ftl;
    int victim_block = -1;
    ftl_init(&ftl);

    assert(nand_program(&ftl.nand, 0, 0, 100) == NAND_OK);
    assert(nand_invalidate(&ftl.nand, 0, 0) == NAND_OK);
    assert(nand_program(&ftl.nand, 1, 0, 200) == NAND_OK);
    assert(nand_invalidate(&ftl.nand, 1, 0) == NAND_OK);

    assert(ftl_gc_select_victim(&ftl, &victim_block) == FTL_OK);
    assert(victim_block == 0);
}

static void test_wl_gc_collect_preserves_data(void)
{
    FTL ftl;
    FTLWearStats stats = {0};
    uint32_t value = 0;
    seed_wl_equal_cost_layout(&ftl);

    assert(ftl_gc_collect(&ftl) == FTL_OK);
    assert(ftl.nand.blocks[0].erase_count == 3);
    assert(ftl.nand.blocks[1].erase_count == 1);
    assert(ftl.l2p[0] == 1);
    assert(ftl.l2p[1] != 5);

    assert(ftl_read(&ftl, 0, &value) == FTL_OK);
    assert(value == 101);
    assert(ftl_read(&ftl, 1, &value) == FTL_OK);
    assert(value == 201);

    assert(ftl_wl_get_stats(&ftl, &stats) == FTL_OK);
    assert(stats.min_erase_count == 1);
    assert(stats.max_erase_count == 3);
    assert(stats.wear_gap == 2);
}

// Stage 5 - Step 3: Static Wear Leveling
static void seed_static_cold_block(FTL *ftl)
{
    ftl_init(ftl);

    for (int i = 0; i < 8; i++) {
        assert(nand_erase(&ftl->nand, 1) == NAND_OK);
    }
    for (int i = 0; i < 10; i++) {
        assert(nand_erase(&ftl->nand, 2) == NAND_OK);
    }
    for (int lpn = 0; lpn < 4; lpn++) {
        assert(ftl_write(ftl, lpn, (uint32_t)(100 + lpn)) == FTL_OK);
    }
}

static void test_wl_static_moves_cold_block(void)
{
    FTL ftl;
    FTLWearStats stats = {0};
    uint32_t value = 0;
    seed_static_cold_block(&ftl);

    assert(ftl_wl_get_stats(&ftl, &stats) == FTL_OK);
    assert(stats.wear_gap == 10);
    assert(ftl_wl_static_collect(&ftl) == FTL_OK);
    assert(ftl.nand.blocks[0].erase_count == 1);
    assert(ftl.nand.blocks[1].erase_count == 8);
    assert(ftl.nand.blocks[2].erase_count == 10);
    assert(ftl.next_free_ppn == 0);

    for (int lpn = 0; lpn < 4; lpn++) {
        assert(ftl.l2p[lpn] == 4 + lpn);
        assert(ftl_read(&ftl, lpn, &value) == FTL_OK);
        assert(value == (uint32_t)(100 + lpn));
        assert(ftl.nand.blocks[0].pages[lpn].state == NAND_PAGE_ERASED);
    }

    assert(ftl_wl_get_stats(&ftl, &stats) == FTL_OK);
    assert(stats.wear_gap == 9);
}

static void test_wl_static_reuses_reclaimed_block(void)
{
    FTL ftl;
    uint32_t value = 0;
    seed_static_cold_block(&ftl);

    assert(ftl_wl_static_collect(&ftl) == FTL_OK);
    assert(ftl.next_free_ppn == 0);
    assert(ftl_write(&ftl, 0, 900) == FTL_OK);
    assert(ftl.l2p[0] == 0);
    assert(ftl_read(&ftl, 0, &value) == FTL_OK);
    assert(value == 900);
    for (int lpn = 1; lpn < 4; lpn++) {
        assert(ftl_read(&ftl, lpn, &value) == FTL_OK);
        assert(value == (uint32_t)(100 + lpn));
    }
}

static void test_wl_static_below_threshold(void)
{
    FTL ftl;
    uint32_t value = 0;
    ftl_init(&ftl);

    assert(ftl_write(&ftl, 0, 123) == FTL_OK);
    assert(ftl_wl_static_collect(&ftl) == FTL_ERROR_NO_WL_CANDIDATE);
    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl.l2p[0] == 0);
    assert(ftl_read(&ftl, 0, &value) == FTL_OK);
    assert(value == 123);
}

static void test_wl_static_insufficient_space(void)
{
    FTL ftl;
    uint32_t value = 0;
    ftl_init(&ftl);

    for (int i = 0; i < 9; i++) {
        assert(nand_erase(&ftl.nand, 1) == NAND_OK);
        assert(nand_erase(&ftl.nand, 2) == NAND_OK);
    }
    for (int lpn = 0; lpn < 7; lpn++) {
        int ppn = lpn;
        assert(nand_program(&ftl.nand, ppn / NAND_PAGES_PER_BLOCK, ppn % NAND_PAGES_PER_BLOCK, (uint32_t)(500 + lpn)) == NAND_OK);
        ftl.l2p[lpn] = ppn;
    }
    assert(nand_program(&ftl.nand, 2, 0, 507) == NAND_OK);
    ftl.l2p[7] = 8;
    assert(nand_program(&ftl.nand, 1, 3, 999) == NAND_OK);
    assert(nand_invalidate(&ftl.nand, 1, 3) == NAND_OK);
    assert(nand_program(&ftl.nand, 2, 1, 999) == NAND_OK);
    assert(nand_invalidate(&ftl.nand, 2, 1) == NAND_OK);

    assert(ftl_wl_static_collect(&ftl) == FTL_ERROR_NO_FREE_PAGE);
    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl.nand.blocks[1].erase_count == 9);
    assert(ftl.nand.blocks[2].erase_count == 9);
    assert(ftl.nand.blocks[0].pages[0].state == NAND_PAGE_PROGRAMMED);
    assert(ftl.nand.blocks[0].pages[3].state == NAND_PAGE_PROGRAMMED);
    assert(ftl.nand.blocks[2].pages[2].state == NAND_PAGE_ERASED);
    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        assert(ftl.l2p[lpn] == (lpn == 7 ? 8 : lpn));
        assert(ftl_read(&ftl, lpn, &value) == FTL_OK);
        assert(value == (uint32_t)(500 + lpn));
    }
}

static void test_wl_static_skips_empty_cold_block(void)
{
    FTL ftl;
    uint32_t value = 0;
    ftl_init(&ftl);

    for (int i = 0; i < 8; i++) {
        assert(nand_erase(&ftl.nand, 1) == NAND_OK);
    }
    for (int i = 0; i < 10; i++) {
        assert(nand_erase(&ftl.nand, 2) == NAND_OK);
    }
    assert(nand_program(&ftl.nand, 1, 0, 700) == NAND_OK);
    ftl.l2p[0] = 4;

    assert(ftl_wl_static_collect(&ftl) == FTL_ERROR_NO_WL_CANDIDATE);
    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl.nand.blocks[1].erase_count == 8);
    assert(ftl_read(&ftl, 0, &value) == FTL_OK);
    assert(value == 700);
}

static void test_wl_static_rejects_invalid_state(void)
{
    FTL ftl;
    seed_static_cold_block(&ftl);

    ftl.l2p[0] = FTL_PHYSICAL_PAGE_COUNT;
    assert(ftl_wl_static_collect(&ftl) == FTL_ERROR_NAND);
    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl.nand.blocks[0].pages[0].state == NAND_PAGE_PROGRAMMED);
    assert(ftl.nand.blocks[0].pages[0].data == 100);

    ftl.l2p[0] = 0;
    ftl.next_free_ppn = -1;
    assert(ftl_wl_static_collect(&ftl) == FTL_ERROR_NAND);
    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl_wl_static_collect(NULL) == FTL_ERROR_NULL_POINTER);
}

static void test_wl_static_equal_wear_prefers_fewer_valid(void)
{
    FTL ftl;
    uint32_t value = 0;
    ftl_init(&ftl);

    for (int i = 0; i < 10; i++) {
        assert(nand_erase(&ftl.nand, 2) == NAND_OK);
    }
    assert(nand_program(&ftl.nand, 0, 0, 100) == NAND_OK);
    assert(nand_program(&ftl.nand, 0, 1, 101) == NAND_OK);
    assert(nand_program(&ftl.nand, 1, 0, 102) == NAND_OK);
    ftl.l2p[0] = 0;
    ftl.l2p[1] = 1;
    ftl.l2p[2] = 4;

    assert(ftl_wl_static_collect(&ftl) == FTL_OK);
    assert(ftl.nand.blocks[0].erase_count == 0);
    assert(ftl.nand.blocks[1].erase_count == 1);
    assert(ftl_read(&ftl, 2, &value) == FTL_OK);
    assert(value == 102);
    assert(ftl.l2p[2] != 4);
}

 // Stage 5 - Step 4: Performance counters
static void test_wl_perf_initial(void)
{
    FTL ftl;
    ftl_init(&ftl);

    assert(ftl.perf.host_writes == 0);
    assert(ftl.perf.nand_programs == 0);
    assert(ftl.perf.gc_migrations == 0);
    assert(ftl.perf.wl_migrations == 0);
    assert(ftl.perf.gc_cycles == 0);
    assert(ftl.perf.wl_cycles == 0);
}

static void test_wl_perf_host_and_gc(void)
{
    FTL ftl;
    ftl_init(&ftl);

    assert(ftl_write(&ftl, 0, 100) == FTL_OK);
    assert(ftl_write(&ftl, 0, 200) == FTL_OK);
    assert(ftl_gc_collect(&ftl) == FTL_OK);
    assert(ftl.perf.host_writes == 2);
    assert(ftl.perf.gc_migrations == 1);
    assert(ftl.perf.wl_migrations == 0);
    assert(ftl.perf.nand_programs == 3);
    assert(ftl.perf.gc_cycles == 1);
    assert(ftl.perf.wl_cycles == 0);
    assert(ftl.nand.blocks[0].erase_count == 1);
}

static void test_wl_perf_static_migration(void)
{
    FTL ftl;
    seed_static_cold_block(&ftl);

    assert(ftl.perf.host_writes == 4);
    assert(ftl.perf.nand_programs == 4);
    assert(ftl_wl_static_collect(&ftl) == FTL_OK);
    assert(ftl.perf.host_writes == 4);
    assert(ftl.perf.gc_migrations == 0);
    assert(ftl.perf.wl_migrations == 4);
    assert(ftl.perf.nand_programs == 8);
    assert(ftl.perf.gc_cycles == 0);
    assert(ftl.perf.wl_cycles == 1);
    assert(ftl.nand.blocks[0].erase_count == 1);
}

static void test_wl_perf_rejected_operations(void)
{
    FTL ftl;
    ftl_init(&ftl);

    assert(ftl_write(&ftl, -1, 100) == FTL_ERROR_INVALID_LPN);
    assert(ftl_gc_collect(&ftl) == FTL_ERROR_NO_GC_VICTIM);
    assert(ftl_wl_static_collect(&ftl) == FTL_ERROR_NO_WL_CANDIDATE);
    assert(ftl.perf.host_writes == 0);
    assert(ftl.perf.nand_programs == 0);
    assert(ftl.perf.gc_migrations == 0);
    assert(ftl.perf.wl_migrations == 0);
    assert(ftl.perf.gc_cycles == 0);
    assert(ftl.perf.wl_cycles == 0);
}

void run_wl_tests(void)
{
    RUN_TEST(test_wl_stats_initial);
    RUN_TEST(test_wl_stats_uneven);
    RUN_TEST(test_wl_stats_after_gc);
    RUN_TEST(test_wl_stats_null_pointer);
    RUN_TEST(test_wl_gc_prefers_less_worn_block);
    RUN_TEST(test_wl_gc_invalid_priority);
    RUN_TEST(test_wl_gc_valid_priority);
    RUN_TEST(test_wl_gc_equal_wear_keeps_order);
    RUN_TEST(test_wl_gc_collect_preserves_data);
    RUN_TEST(test_wl_static_moves_cold_block);
    RUN_TEST(test_wl_static_reuses_reclaimed_block);
    RUN_TEST(test_wl_static_below_threshold);
    RUN_TEST(test_wl_static_insufficient_space);
    RUN_TEST(test_wl_static_skips_empty_cold_block);
    RUN_TEST(test_wl_static_rejects_invalid_state);
    RUN_TEST(test_wl_static_equal_wear_prefers_fewer_valid);
    RUN_TEST(test_wl_perf_initial);
    RUN_TEST(test_wl_perf_host_and_gc);
    RUN_TEST(test_wl_perf_static_migration);
    RUN_TEST(test_wl_perf_rejected_operations);
}
