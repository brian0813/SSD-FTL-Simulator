#ifndef WORKLOAD_H
#define WORKLOAD_H

#include <stdint.h>

typedef enum {
    WORKLOAD_SEQUENTIAL = 0,
    WORKLOAD_UNIFORM_RANDOM,
    WORKLOAD_HOT_COLD
} WorkloadType;

typedef struct {
    WorkloadType type;
    int active_lpn_count;
    int hot_lpn_count;
    int hot_write_percent;
    uint32_t seed;
} WorkloadConfig;

typedef struct {
    WorkloadConfig config;
    uint32_t random_state;
    int next_sequential_lpn;
} WorkloadGenerator;

int workload_init(
    WorkloadGenerator *generator,
    const WorkloadConfig *config
);

int workload_next_lpn(WorkloadGenerator *generator);

#endif