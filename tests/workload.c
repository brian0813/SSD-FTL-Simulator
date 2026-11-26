#include "workload.h"
#include "ftl.h"
#include <stddef.h>

static uint32_t next_random(uint32_t *seed)
{
    *seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);

    uint32_t value = *seed;

    value ^= value >> 16;
    value *= UINT32_C(0x85ebca6b);
    value ^= value >> 13;
    value *= UINT32_C(0xc2b2ae35);
    value ^= value >> 16;

    return value;
}

static uint32_t random_bounded(uint32_t *seed, uint32_t bound)
{
    uint32_t threshold = (uint32_t)(0U - bound) % bound;
    uint32_t value;

    do {
        value = next_random(seed);
    } while (value < threshold);

    return value % bound;
}

static int is_valid_config(const WorkloadConfig *config)
{
    if (config == NULL) {
        return 0;
    }

    if (config->type != WORKLOAD_SEQUENTIAL &&
        config->type != WORKLOAD_UNIFORM_RANDOM &&
        config->type != WORKLOAD_HOT_COLD) {
        return 0;
    }

    if (config->active_lpn_count <= 0 ||
        config->active_lpn_count > FTL_LOGICAL_PAGE_COUNT) {
        return 0;
    }

    if (config->type == WORKLOAD_HOT_COLD) {
        if (config->hot_lpn_count <= 0 ||
            config->hot_lpn_count >= config->active_lpn_count ||
            config->hot_write_percent < 0 ||
            config->hot_write_percent > 100) {
            return 0;
        }
    }

    return 1;
}

int workload_init(WorkloadGenerator *generator, const WorkloadConfig *config)
{
    if (generator == NULL || !is_valid_config(config)) {
        return 0;
    }

    generator->config = *config;
    generator->random_state = config->seed;
    generator->next_sequential_lpn = 0;

    return 1;
}

int workload_next_lpn(WorkloadGenerator *generator)
{
    if (generator == NULL || !is_valid_config(&generator->config)) {
        return FTL_INVALID_PPN;
    }

    const WorkloadConfig *config = &generator->config;

    switch (config->type) {
        case WORKLOAD_SEQUENTIAL: {
            int lpn = generator->next_sequential_lpn;
            generator->next_sequential_lpn = (lpn + 1) % config->active_lpn_count;
            return lpn;
        }

        case WORKLOAD_UNIFORM_RANDOM:
            return (int)random_bounded(&generator->random_state, (uint32_t)config->active_lpn_count);

        case WORKLOAD_HOT_COLD: {
            uint32_t probability = random_bounded(&generator->random_state, 100U);

            if (probability < (uint32_t)config->hot_write_percent) {
                return (int)random_bounded(&generator->random_state, (uint32_t)config->hot_lpn_count);
            }

            int cold_lpn_count = config->active_lpn_count - config->hot_lpn_count;
            int cold_offset = (int)random_bounded(&generator->random_state, (uint32_t)cold_lpn_count);

            return config->hot_lpn_count + cold_offset;
        }

        default:
            return FTL_INVALID_PPN;
    }
}