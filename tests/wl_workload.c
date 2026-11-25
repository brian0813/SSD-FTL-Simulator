#include "ftl.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#define WORKLOAD_UPDATES 1200
#define WL_CHECK_INTERVAL 16
#define HOT_LPN_COUNT 3

typedef struct {
    FTLWearStats wear;
    FTLPerfStats perf;
    uint32_t block_erases[NAND_BLOCK_COUNT];
    uint64_t total_erases;
    uint64_t initial_erases;
    uint64_t wl_attempts;
    uint64_t wl_no_candidate;
    uint64_t wl_no_space;
} WorkloadResult;

static uint32_t next_random(uint32_t *seed)
{
    *seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);
    return *seed;
}

static int verify_integrity(const FTL *ftl, const uint32_t expected[FTL_LOGICAL_PAGE_COUNT], int active_lpn_count)
{
    int owners[FTL_PHYSICAL_PAGE_COUNT] = {0};

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        uint32_t value = 0;
        int ppn = ftl->l2p[lpn];
        if (lpn >= active_lpn_count) {
            if (ppn != FTL_INVALID_PPN || ftl_read(ftl, lpn, &value) != FTL_ERROR_UNMAPPED_LPN) {
                return 0;
            }
            continue;
        }
        if (ftl_read(ftl, lpn, &value) != FTL_OK || value != expected[lpn] || ppn < 0 || ppn >= FTL_PHYSICAL_PAGE_COUNT) {
            return 0;
        }
        owners[ppn]++;
    }

    for (int ppn = 0; ppn < FTL_PHYSICAL_PAGE_COUNT; ppn++) {
        int block = ppn / NAND_PAGES_PER_BLOCK;
        int page = ppn % NAND_PAGES_PER_BLOCK;
        NANDPageState state = ftl->nand.blocks[block].pages[page].state;
        if (state == NAND_PAGE_PROGRAMMED) {
            if (owners[ppn] != 1) {
                return 0;
            }
        } else if (state == NAND_PAGE_ERASED || state == NAND_PAGE_INVALID) {
            if (owners[ppn] != 0) {
                return 0;
            }
        } else {
            return 0;
        }
    }
    return 1;
}

static int run_workload(int enable_wl, int hot_percent, int active_lpn_count, int seeded_cold, uint32_t seed, WorkloadResult *out)
{
    FTL ftl;
    uint32_t expected[FTL_LOGICAL_PAGE_COUNT] = {0};
    ftl_init(&ftl);
    *out = (WorkloadResult){0};

    if (seeded_cold) {
        for (int i = 0; i < 8; i++) {
            if (nand_erase(&ftl.nand, 1) != NAND_OK) {
                return 0;
            }
        }
        for (int i = 0; i < 10; i++) {
            if (nand_erase(&ftl.nand, 2) != NAND_OK) {
                return 0;
            }
        }
        out->initial_erases = 18;
    }

    for (int lpn = 0; lpn < active_lpn_count; lpn++) {
        if (seeded_cold && lpn == 2) {
            ftl.next_free_ppn = NAND_PAGES_PER_BLOCK;
        }
        expected[lpn] = (uint32_t)(1000 + lpn);
        if (ftl_write(&ftl, lpn, expected[lpn]) != FTL_OK) {
            return 0;
        }
    }

    if (seeded_cold && enable_wl) {
        out->wl_attempts++;
        if (ftl_wl_static_collect(&ftl) != FTL_OK) {
            return 0;
        }
    }

    for (int i = 0; i < WORKLOAD_UPDATES; i++) {
        uint32_t selector = next_random(&seed) >> 16;
        uint32_t index = next_random(&seed) >> 16;
        int hot_start = seeded_cold ? 2 : 0;
        int hot_count = seeded_cold ? 2 : HOT_LPN_COUNT;
        int cold_start = seeded_cold ? 0 : hot_count;
        int cold_count = active_lpn_count - hot_count;
        int lpn = selector % 100U < (uint32_t)hot_percent ? hot_start + (int)(index % (uint32_t)hot_count) : cold_start + (int)(index % (uint32_t)cold_count);
        expected[lpn] = (uint32_t)(10000 + i);

        FTLStatus status = ftl_write(&ftl, lpn, expected[lpn]);
        if (status != FTL_OK) {
            printf("Failed write: hot=%d, wl=%d, step=%d, status=%d\n", hot_percent, enable_wl, i, status);
            return 0;
        }

        if (enable_wl && (i + 1) % WL_CHECK_INTERVAL == 0) {
            FTLWearStats wear = {0};
            if (ftl_wl_get_stats(&ftl, &wear) != FTL_OK) {
                return 0;
            }
            if (wear.wear_gap >= FTL_WL_STATIC_THRESHOLD) {
                out->wl_attempts++;
                status = ftl_wl_static_collect(&ftl);
                if (status == FTL_ERROR_NO_WL_CANDIDATE) {
                    out->wl_no_candidate++;
                } else if (status == FTL_ERROR_NO_FREE_PAGE) {
                    out->wl_no_space++;
                } else if (status != FTL_OK) {
                    printf("Failed WL: hot=%d, step=%d, status=%d\n", hot_percent, i, status);
                    return 0;
                }
            }
        }

        if (!verify_integrity(&ftl, expected, active_lpn_count)) {
            printf("Data mismatch: hot=%d, wl=%d, step=%d\n", hot_percent, enable_wl, i);
            return 0;
        }
    }

    if (ftl_wl_get_stats(&ftl, &out->wear) != FTL_OK) {
        return 0;
    }
    out->perf = ftl.perf;
    for (int block = 0; block < NAND_BLOCK_COUNT; block++) {
        out->block_erases[block] = ftl.nand.blocks[block].erase_count;
        out->total_erases += out->block_erases[block];
    }

    if (out->perf.host_writes != (uint64_t)active_lpn_count + WORKLOAD_UPDATES ||
        out->perf.nand_programs != out->perf.host_writes + out->perf.gc_migrations + out->perf.wl_migrations) {
        return 0;
    }
    return verify_integrity(&ftl, expected, active_lpn_count);
}

static void print_result(const char *mode, const WorkloadResult *result)
{
    double wa = (double)result->perf.nand_programs / (double)result->perf.host_writes;
    printf("%-11s | gap=%3u | erases=%4" PRIu64 " | WA=%5.3f | GC=%4" PRIu64 " | WL=%3" PRIu64 " | migration GC/WL=%5" PRIu64 "/%-4" PRIu64 " | blocks=%u,%u,%u\n", mode, result->wear.wear_gap, result->total_erases - result->initial_erases, wa, result->perf.gc_cycles, result->perf.wl_cycles, result->perf.gc_migrations, result->perf.wl_migrations, result->block_erases[0], result->block_erases[1], result->block_erases[2]);
}

int main(void)
{
    const int hot_percents[] = {90, 70, 50, 90, 70, 50, 90, 50};
    const int active_lpn_counts[] = {8, 8, 8, 5, 5, 5, 4, 4};
    for (int i = 0; i < 8; i++) {
        WorkloadResult baseline = {0};
        WorkloadResult static_wl = {0};
        int hot_percent = hot_percents[i];
        int active_lpn_count = active_lpn_counts[i];
        int seeded_cold = i >= 6;
        uint32_t seed = UINT32_C(0x10203040) + (uint32_t)(i % 3);
        if (!run_workload(0, hot_percent, active_lpn_count, seeded_cold, seed, &baseline) || !run_workload(1, hot_percent, active_lpn_count, seeded_cold, seed, &static_wl)) {
            printf("Workload FAILED for %d%% hot writes, %d LPNs\n", hot_percent, active_lpn_count);
            return 1;
        }

        printf("\nWorkload %d active LPN, %d%% hot / %d%% cold%s (%d updates per mode):\n", active_lpn_count, hot_percent, 100 - hot_percent, seeded_cold ? ", pre-seeded uneven wear" : "", WORKLOAD_UPDATES);
        print_result("GC only", &baseline);
        print_result("GC + WL", &static_wl);
        printf("WL attempts=%" PRIu64 ", no-candidate=%" PRIu64 ", no-space=%" PRIu64 "\n", static_wl.wl_attempts, static_wl.wl_no_candidate, static_wl.wl_no_space);
    }
    puts("\nAll mixed workload runs passed integrity checks.");
    return 0;
}
