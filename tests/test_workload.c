#include "workload.h"
#include "ftl.h"
#include "test_suite.h"
#include "test_support.h"
#include <assert.h>
#include <stddef.h>

static void test_workload_sequential_wrap(void)
{
    WorkloadConfig config = {
        .type = WORKLOAD_SEQUENTIAL,
        .active_lpn_count = 4,
        .seed = 12345U
    };

    WorkloadGenerator generator;

    assert(workload_init(&generator, &config) == 1);

    for (int i = 0; i < 12; i++) {
        assert(workload_next_lpn(&generator) == i % 4);
    }
}

static void test_workload_single_active_lpn(void)
{
    WorkloadConfig config = {
        .type = WORKLOAD_SEQUENTIAL,
        .active_lpn_count = 1,
        .seed = 12345U
    };

    WorkloadGenerator generator;

    assert(workload_init(&generator, &config) == 1);

    for (int i = 0; i < 100; i++) {
        assert(workload_next_lpn(&generator) == 0);
    }
}

static void test_workload_uniform_random_range(void)
{
    WorkloadConfig config = {
        .type = WORKLOAD_UNIFORM_RANDOM,
        .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
        .seed = 12345U
    };

    WorkloadGenerator generator;
    int counts[FTL_LOGICAL_PAGE_COUNT] = {0};

    assert(workload_init(&generator, &config) == 1);

    for (int i = 0; i < 10000; i++) {
        int lpn = workload_next_lpn(&generator);

        assert(lpn >= 0 && lpn < config.active_lpn_count);
        counts[lpn]++;
    }

    for (int lpn = 0; lpn < config.active_lpn_count; lpn++) {
        assert(counts[lpn] > 0);
    }
}

static void test_workload_reproducibility(void)
{
    WorkloadConfig config = {
        .type = WORKLOAD_HOT_COLD,
        .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
        .hot_lpn_count = 2,
        .hot_write_percent = 90,
        .seed = 12345U
    };

    WorkloadGenerator first;
    WorkloadGenerator second;

    assert(workload_init(&first, &config) == 1);
    assert(workload_init(&second, &config) == 1);

    for (int i = 0; i < 10000; i++) {
        assert(workload_next_lpn(&first) == workload_next_lpn(&second));
    }
}

static void test_workload_hot_only(void)
{
    WorkloadConfig config = {
        .type = WORKLOAD_HOT_COLD,
        .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
        .hot_lpn_count = 2,
        .hot_write_percent = 100,
        .seed = 12345U
    };

    WorkloadGenerator generator;

    assert(workload_init(&generator, &config) == 1);

    for (int i = 0; i < 1000; i++) {
        int lpn = workload_next_lpn(&generator);

        assert(lpn >= 0 && lpn < config.hot_lpn_count);
    }
}

static void test_workload_cold_only(void)
{
    WorkloadConfig config = {
        .type = WORKLOAD_HOT_COLD,
        .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
        .hot_lpn_count = 2,
        .hot_write_percent = 0,
        .seed = 12345U
    };

    WorkloadGenerator generator;

    assert(workload_init(&generator, &config) == 1);

    for (int i = 0; i < 1000; i++) {
        int lpn = workload_next_lpn(&generator);

        assert(lpn >= config.hot_lpn_count && lpn < config.active_lpn_count);
    }
}

static void test_workload_hot_cold_distribution(void)
{
    WorkloadConfig config = {
        .type = WORKLOAD_HOT_COLD,
        .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
        .hot_lpn_count = 2,
        .hot_write_percent = 90,
        .seed = 12345U
    };

    WorkloadGenerator generator;
    int hot_count = 0;
    int cold_count = 0;

    assert(workload_init(&generator, &config) == 1);

    for (int i = 0; i < 10000; i++) {
        int lpn = workload_next_lpn(&generator);

        assert(lpn >= 0 && lpn < config.active_lpn_count);

        if (lpn < config.hot_lpn_count) {
            hot_count++;
        } else {
            cold_count++;
        }
    }

    assert(hot_count + cold_count == 10000);
    assert(hot_count >= 8800 && hot_count <= 9200);
    assert(cold_count >= 800 && cold_count <= 1200);
}

static void test_workload_invalid_config(void)
{
    WorkloadConfig config = {
        .type = WORKLOAD_HOT_COLD,
        .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
        .hot_lpn_count = 2,
        .hot_write_percent = 90,
        .seed = 12345U
    };

    WorkloadGenerator generator;

    assert(workload_init(NULL, &config) == 0);
    assert(workload_init(&generator, NULL) == 0);
    assert(workload_next_lpn(NULL) == FTL_INVALID_PPN);

    config.active_lpn_count = 0;
    assert(workload_init(&generator, &config) == 0);

    config.active_lpn_count = FTL_LOGICAL_PAGE_COUNT + 1;
    assert(workload_init(&generator, &config) == 0);

    config.active_lpn_count = FTL_LOGICAL_PAGE_COUNT;

    config.hot_lpn_count = 0;
    assert(workload_init(&generator, &config) == 0);

    config.hot_lpn_count = config.active_lpn_count;
    assert(workload_init(&generator, &config) == 0);

    config.hot_lpn_count = 2;

    config.hot_write_percent = -1;
    assert(workload_init(&generator, &config) == 0);

    config.hot_write_percent = 101;
    assert(workload_init(&generator, &config) == 0);

    config.hot_write_percent = 90;

    config.type = (WorkloadType)99;
    assert(workload_init(&generator, &config) == 0);
}

static void test_workload_hot_lpn_distribution(void)
{
    WorkloadConfig config = {
        .type = WORKLOAD_HOT_COLD,
        .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
        .hot_lpn_count = 2,
        .hot_write_percent = 100,
        .seed = 12345U
    };

    WorkloadGenerator generator;
    int counts[2] = {0};

    assert(workload_init(&generator, &config) == 1);

    for (int i = 0; i < 10000; i++) {
        int lpn = workload_next_lpn(&generator);

        assert(lpn >= 0 && lpn < 2);
        counts[lpn]++;
    }

    assert(counts[0] + counts[1] == 10000);
    assert(counts[0] >= 4700 && counts[0] <= 5300);
    assert(counts[1] >= 4700 && counts[1] <= 5300);
}

void run_workload_tests(void)
{
    RUN_TEST(test_workload_sequential_wrap);
    RUN_TEST(test_workload_single_active_lpn);
    RUN_TEST(test_workload_uniform_random_range);
    RUN_TEST(test_workload_reproducibility);
    RUN_TEST(test_workload_hot_only);
    RUN_TEST(test_workload_cold_only);
    RUN_TEST(test_workload_hot_cold_distribution);
    RUN_TEST(test_workload_invalid_config);
    RUN_TEST(test_workload_hot_lpn_distribution);
}