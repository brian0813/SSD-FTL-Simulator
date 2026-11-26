#include "performance.h"
#include "test_suite.h"
#include "test_support.h"
#include <stddef.h>
#include <math.h>

static void test_performance_sequential(void)
{
    PerformanceConfig config = {
        .workload = {
            .type = WORKLOAD_SEQUENTIAL,
            .active_lpn_count = 4,
            .seed = 12345U
        },
        .update_count = 100,
        .enable_static_wl = 0
    };

    PerformanceResult result = {0};

    assert(performance_run(&config, &result) == 1);
    assert(result.perf.host_writes == 100);
    assert(result.perf.nand_programs >= 100);
    assert(result.perf.wl_cycles == 0);
    assert(result.perf.wl_migrations == 0);
}

static void test_performance_uniform_random(void)
{
    PerformanceConfig config = {
        .workload = {
            .type = WORKLOAD_UNIFORM_RANDOM,
            .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
            .seed = 12345U
        },
        .update_count = 1200,
        .enable_static_wl = 0
    };

    PerformanceResult result = {0};

    assert(performance_run(&config, &result) == 1);
    assert(result.perf.host_writes == 1200);
    assert(result.perf.nand_programs >= 1200);
}

static void test_performance_hot_cold(void)
{
    PerformanceConfig config = {
        .workload = {
            .type = WORKLOAD_HOT_COLD,
            .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
            .hot_lpn_count = 2,
            .hot_write_percent = 90,
            .seed = 12345U
        },
        .update_count = 1200,
        .enable_static_wl = 0
    };

    PerformanceResult result = {0};

    assert(performance_run(&config, &result) == 1);
    assert(result.perf.host_writes == 1200);
    assert(result.perf.nand_programs >= 1200);
}

static void test_performance_initial_writes_excluded(void)
{
    PerformanceConfig config = {
        .workload = {
            .type = WORKLOAD_SEQUENTIAL,
            .active_lpn_count = 2,
            .seed = 12345U
        },
        .update_count = 1,
        .enable_static_wl = 0
    };

    PerformanceResult result = {0};

    assert(performance_run(&config, &result) == 1);
    assert(result.perf.host_writes == 1);
    assert(result.perf.nand_programs == 1);
    assert(result.total_erases == 0);
    assert(result.wear_gap == 0);
    assert(result.write_amplification == 1.0);
    assert(result.average_erase_count == 0.0);
    assert(result.erase_stddev == 0.0);
}

static void test_performance_metrics(void)
{
    PerformanceConfig config = {
        .workload = {
            .type = WORKLOAD_HOT_COLD,
            .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
            .hot_lpn_count = 2,
            .hot_write_percent = 90,
            .seed = 12345U
        },
        .update_count = 1200,
        .enable_static_wl = 0
    };

    PerformanceResult result = {0};

    assert(performance_run(&config, &result) == 1);

    assert(result.perf.nand_programs == result.perf.host_writes + result.perf.gc_migrations + result.perf.wl_migrations);
    assert(result.total_erases == result.perf.gc_cycles + result.perf.wl_cycles);

    double expected_wa = (double)result.perf.nand_programs / result.perf.host_writes;

    assert(fabs(result.write_amplification - expected_wa) < 1e-9);

    uint64_t total = 0;
    uint32_t minimum = UINT32_MAX;
    uint32_t maximum = 0;

    for (int block = 0; block < NAND_BLOCK_COUNT; block++) {
        uint32_t count = result.block_erase_counts[block];

        total += count;

        if (count < minimum) {
            minimum = count;
        }

        if (count > maximum) {
            maximum = count;
        }
    }

    assert(result.wear_gap == maximum - minimum);
    assert(fabs(result.average_erase_count - (double)total / NAND_BLOCK_COUNT) < 1e-9);

    double variance = 0.0;

    for (int block = 0; block < NAND_BLOCK_COUNT; block++) {
        double difference = (double)result.block_erase_counts[block] - result.average_erase_count;

        variance += difference * difference;
    }

    variance /= NAND_BLOCK_COUNT;

    assert(fabs(result.erase_stddev - sqrt(variance)) < 1e-9);
}

static void test_performance_static_wl(void)
{
    PerformanceConfig config = {
        .workload = {
            .type = WORKLOAD_HOT_COLD,
            .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
            .hot_lpn_count = 2,
            .hot_write_percent = 90,
            .seed = 12345U
        },
        .update_count = 1200,
        .enable_static_wl = 1
    };

    PerformanceResult result = {0};

    assert(performance_run(&config, &result) == 1);
    assert(result.perf.host_writes == 1200);
    assert(result.perf.nand_programs == result.perf.host_writes + result.perf.gc_migrations + result.perf.wl_migrations);
    assert(result.total_erases == result.perf.gc_cycles + result.perf.wl_cycles);
}

static void test_performance_invalid_config(void)
{
    PerformanceConfig config = {
        .workload = {
            .type = WORKLOAD_SEQUENTIAL,
            .active_lpn_count = 4,
            .seed = 12345U
        },
        .update_count = 100,
        .enable_static_wl = 0
    };

    PerformanceResult result = {0};

    assert(performance_run(NULL, &result) == 0);
    assert(performance_run(&config, NULL) == 0);

    config.update_count = 0;
    assert(performance_run(&config, &result) == 0);

    config.update_count = -1;
    assert(performance_run(&config, &result) == 0);

    config.update_count = 100;
    config.enable_static_wl = 2;
    assert(performance_run(&config, &result) == 0);

    config.enable_static_wl = -1;
    assert(performance_run(&config, &result) == 0);

    config.enable_static_wl = 0;
    config.workload.active_lpn_count = 0;
    assert(performance_run(&config, &result) == 0);

    config.workload.active_lpn_count = FTL_LOGICAL_PAGE_COUNT + 1;
    assert(performance_run(&config, &result) == 0);
}

void run_performance_tests(void)
{
    RUN_TEST(test_performance_sequential);
    RUN_TEST(test_performance_uniform_random);
    RUN_TEST(test_performance_hot_cold);
    RUN_TEST(test_performance_initial_writes_excluded);
    RUN_TEST(test_performance_metrics);
    RUN_TEST(test_performance_static_wl);
    RUN_TEST(test_performance_invalid_config);
}