#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include "workload.h"
#include "ftl.h"

typedef struct {
    WorkloadConfig workload;
    int update_count;
    int enable_static_wl;
} PerformanceConfig;

typedef struct {
    FTLPerfStats perf;
    uint64_t total_erases;
    uint32_t block_erase_counts[NAND_BLOCK_COUNT];
    uint32_t wear_gap;
    double write_amplification;
    double average_erase_count;
    double erase_stddev;
} PerformanceResult;

int performance_run(
    const PerformanceConfig *config,
    PerformanceResult *result
);

#endif