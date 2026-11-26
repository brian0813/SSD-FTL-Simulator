#include "performance.h"
#include <stddef.h>
#include <math.h>

static uint64_t get_total_erases(const FTL *ftl)
{
    uint64_t total = 0;

    for (int block = 0; block < NAND_BLOCK_COUNT; block++) {
        total += ftl->nand.blocks[block].erase_count;
    }

    return total;
}

static FTLPerfStats get_perf_delta(const FTLPerfStats *after, const FTLPerfStats *before)
{
    FTLPerfStats delta = {0};

    delta.host_writes = after->host_writes - before->host_writes;
    delta.nand_programs = after->nand_programs - before->nand_programs;
    delta.gc_migrations = after->gc_migrations - before->gc_migrations;
    delta.wl_migrations = after->wl_migrations - before->wl_migrations;
    delta.gc_cycles = after->gc_cycles - before->gc_cycles;
    delta.wl_cycles = after->wl_cycles - before->wl_cycles;

    return delta;
}

static double calculate_erase_stddev(const uint32_t counts[NAND_BLOCK_COUNT], double average)
{
    double sum = 0.0;

    for (int block = 0; block < NAND_BLOCK_COUNT; block++) {
        double difference = (double)counts[block] - average;
        sum += difference * difference;
    }

    return sqrt(sum / NAND_BLOCK_COUNT);
}

static int verify_data(const FTL *ftl, const uint32_t expected[FTL_LOGICAL_PAGE_COUNT], int active_lpn_count)
{
    for (int lpn = 0; lpn < active_lpn_count; lpn++) {
        uint32_t actual = 0;

        if (ftl_read(ftl, lpn, &actual) != FTL_OK || actual != expected[lpn]) {
            return 0;
        }
    }

    return 1;
}

static int collect_result(const FTL *ftl, const FTLPerfStats *before, uint64_t initial_erases, PerformanceResult *result)
{
    FTLWearStats wear = {0};

    if (ftl_wl_get_stats(ftl, &wear) != FTL_OK) {
        return 0;
    }

    result->perf = get_perf_delta(&ftl->perf, before);

    uint64_t final_erases = get_total_erases(ftl);

    result->total_erases = final_erases - initial_erases;
    result->wear_gap = wear.wear_gap;

    for (int block = 0; block < NAND_BLOCK_COUNT; block++) {
        result->block_erase_counts[block] = ftl->nand.blocks[block].erase_count;
    }

    if (result->perf.host_writes == 0) {
        return 0;
    }

    result->write_amplification = (double)result->perf.nand_programs / result->perf.host_writes;
    result->average_erase_count = (double)final_erases / NAND_BLOCK_COUNT;
    result->erase_stddev = calculate_erase_stddev(result->block_erase_counts, result->average_erase_count);

    return 1;
}

int performance_run(const PerformanceConfig *config, PerformanceResult *result)
{
    if (config == NULL || result == NULL) {
        return 0;
    }

    if (config->update_count <= 0 ||
        (config->enable_static_wl != 0 && config->enable_static_wl != 1)) {
        return 0;
    }

    WorkloadGenerator generator;

    if (!workload_init(&generator, &config->workload)) {
        return 0;
    }

    FTL ftl;
    uint32_t expected[FTL_LOGICAL_PAGE_COUNT] = {0};

    ftl_init(&ftl);

    int active_lpn_count = config->workload.active_lpn_count;

    for (int lpn = 0; lpn < active_lpn_count; lpn++) {
        expected[lpn] = (uint32_t)lpn;

        if (ftl_write(&ftl, lpn, expected[lpn]) != FTL_OK) {
            return 0;
        }
    }

    if (!verify_data(&ftl, expected, active_lpn_count)) {
        return 0;
    }

    FTLPerfStats before = ftl.perf;
    uint64_t initial_erases = get_total_erases(&ftl);

    for (int i = 0; i < config->update_count; i++) {
        int lpn = workload_next_lpn(&generator);

        if (lpn < 0 || lpn >= active_lpn_count) {
            return 0;
        }

        uint32_t data = (uint32_t)i + UINT32_C(1000);

        if (ftl_write(&ftl, lpn, data) != FTL_OK) {
            return 0;
        }

        expected[lpn] = data;

        if (config->enable_static_wl) {
            FTLStatus status = ftl_wl_static_collect(&ftl);

            if (status != FTL_OK &&
                status != FTL_ERROR_NO_WL_CANDIDATE &&
                status != FTL_ERROR_NO_FREE_PAGE) {
                return 0;
            }
        }

        if (!verify_data(&ftl, expected, active_lpn_count)) {
            return 0;
        }
    }

    PerformanceResult output = {0};

    if (!collect_result(&ftl, &before, initial_erases, &output)) {
        return 0;
    }

    *result = output;

    return 1;
}