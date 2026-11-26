#include "performance.h"
#include <stdio.h>
#include <inttypes.h>

static void print_result(const char *name, const PerformanceResult *result)
{
    printf("\n%s\n", name);
    printf("----------------------------------------\n");

    printf("Host writes       : %" PRIu64 "\n", result->perf.host_writes);
    printf("NAND programs     : %" PRIu64 "\n", result->perf.nand_programs);
    printf("GC cycles         : %" PRIu64 "\n", result->perf.gc_cycles);
    printf("WL cycles         : %" PRIu64 "\n", result->perf.wl_cycles);
    printf("GC migrations     : %" PRIu64 "\n", result->perf.gc_migrations);
    printf("WL migrations     : %" PRIu64 "\n", result->perf.wl_migrations);
    printf("Total erases      : %" PRIu64 "\n", result->total_erases);
    printf("Write amplification: %.3f\n", result->write_amplification);
    printf("Wear gap          : %" PRIu32 "\n", result->wear_gap);
    printf("Average erase     : %.3f\n", result->average_erase_count);
    printf("Erase stddev      : %.3f\n", result->erase_stddev);

    for (int block = 0; block < NAND_BLOCK_COUNT; block++) {
        printf("Block %d erases    : %" PRIu32 "\n", block, result->block_erase_counts[block]);
    }
}

int main(void)
{
    PerformanceConfig config = {
        .workload = {
            .type = WORKLOAD_SEQUENTIAL,
            .active_lpn_count = FTL_LOGICAL_PAGE_COUNT,
            .hot_lpn_count = 2,
            .hot_write_percent = 90,
            .seed = 12345U
        },
        .update_count = 1200,
        .enable_static_wl = 0
    };

    PerformanceResult result = {0};

    if (!performance_run(&config, &result)) {
        printf("Sequential workload failed.\n");
        return 1;
    }

    print_result("Sequential workload", &result);

    config.workload.type = WORKLOAD_UNIFORM_RANDOM;

    if (!performance_run(&config, &result)) {
        printf("Uniform random workload failed.\n");
        return 1;
    }

    print_result("Uniform random workload", &result);

    config.workload.type = WORKLOAD_HOT_COLD;

    if (!performance_run(&config, &result)) {
        printf("Hot/cold workload failed.\n");
        return 1;
    }

    print_result("Hot/cold workload", &result);

    return 0;
}