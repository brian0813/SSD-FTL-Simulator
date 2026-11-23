#include "ftl.h"
#include <stdint.h>
#include <stdio.h>

#define STRESS_RUN_COUNT 500
#define STRESS_UPDATES_PER_RUN 2000

static uint32_t next_random(uint32_t *seed)
{
    *seed = *seed * UINT32_C(1664525) + UINT32_C(1013904223);
    return *seed;
}

static int verify_integrity(const FTL *ftl, const uint32_t expected[FTL_LOGICAL_PAGE_COUNT])
{
    int owners[FTL_PHYSICAL_PAGE_COUNT] = {0};

    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        uint32_t value = 0;
        if (ftl_read(ftl, lpn, &value) != FTL_OK || value != expected[lpn]) {
            return 0;
        }
        int ppn = ftl->l2p[lpn];
        if (ppn < 0 || ppn >= FTL_PHYSICAL_PAGE_COUNT) {
            return 0;
        }
        owners[ppn]++;
    }

    for (int ppn = 0; ppn < FTL_PHYSICAL_PAGE_COUNT; ppn++) {
        int block = ppn / NAND_PAGES_PER_BLOCK;
        int page = ppn % NAND_PAGES_PER_BLOCK;
        NANDPageState state = ftl->nand.blocks[block].pages[page].state;
        int expected_owners = (state == NAND_PAGE_PROGRAMMED) ? 1 : 0;

        if (owners[ppn] != expected_owners) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    unsigned long long successful_writes = 0;
    unsigned long long no_free_page = 0;

    for (int run = 0; run < STRESS_RUN_COUNT; run++) {
        FTL ftl;
        uint32_t expected[FTL_LOGICAL_PAGE_COUNT];
        uint32_t seed = UINT32_C(1234567) + (uint32_t)run;
        ftl_init(&ftl);

        for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
            expected[lpn] = (uint32_t)(1000 + lpn);
            if (ftl_write(&ftl, lpn, expected[lpn]) != FTL_OK) {
                printf("Initialization failed: run=%d lpn=%d\n", run, lpn);
                return 1;
            }
        }

        for (int i = 0; i < STRESS_UPDATES_PER_RUN; i++) {
            int lpn = (int)(next_random(&seed) % FTL_LOGICAL_PAGE_COUNT);
            uint32_t data = (uint32_t)(3000 + i + STRESS_UPDATES_PER_RUN * run);
            FTLStatus status = ftl_write(&ftl, lpn, data);

            if (status == FTL_OK) {
                expected[lpn] = data;
                successful_writes++;
            } else if (status == FTL_ERROR_NO_FREE_PAGE) {
                no_free_page++;
            } else {
                printf("Unexpected write error: run=%d update=%d status=%d\n", run, i, (int)status);
                return 1;
            }

            if (!verify_integrity(&ftl, expected)) {
                printf("Readback or mapping mismatch: run=%d update=%d\n", run, i);
                return 1;
            }
        }
    }

    printf("Stress PASS: %d runs x %d updates\n", STRESS_RUN_COUNT, STRESS_UPDATES_PER_RUN);
    printf("Successful updates: %llu\n", successful_writes);
    printf("No-free-page responses: %llu\n", no_free_page);
    printf("All LPN readbacks and PPN ownership checks passed.\n");
    return 0;
}