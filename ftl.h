#ifndef FTL_H
#define FTL_H

#include "nand.h"

// Total number of physical NAND pages
#define FTL_PHYSICAL_PAGE_COUNT (NAND_BLOCK_COUNT * NAND_PAGES_PER_BLOCK)

// Reserve one block worth of capacity for updates
#define FTL_RESERVED_PAGE_COUNT NAND_PAGES_PER_BLOCK

// Number of logical pages exposed by the FTL
#define FTL_LOGICAL_PAGE_COUNT (FTL_PHYSICAL_PAGE_COUNT - FTL_RESERVED_PAGE_COUNT)

#define FTL_INVALID_PPN (-1)

typedef enum {
    FTL_OK = 0,
    FTL_ERROR_NULL_POINTER,
    FTL_ERROR_INVALID_LPN,
    FTL_ERROR_UNMAPPED_LPN,
    FTL_ERROR_NO_FREE_PAGE,
    FTL_ERROR_NAND,
    FTL_ERROR_INVALID_BLOCK,
    FTL_ERROR_NO_GC_VICTIM
} FTLStatus;

typedef struct {
    int erased_pages;
    int valid_pages;
    int invalid_pages;
} FTLBlockStats;

typedef struct {
    NANDFlash nand;
    int l2p[FTL_LOGICAL_PAGE_COUNT];
    int next_free_ppn;
} FTL;

void ftl_init(FTL *ftl);

FTLStatus ftl_write(FTL *ftl, int lpn, uint32_t data);

FTLStatus ftl_read(const FTL *ftl, int lpn, uint32_t *data);

FTLStatus ftl_gc_get_block_stats(
    const FTL *ftl,
    int block,
    FTLBlockStats *stats
);

FTLStatus ftl_gc_select_victim(
    const FTL *ftl,
    int *victim_block
);

#endif