#ifndef FTL_H
#define FTL_H

#include "nand.h"

#define FTL_LOGICAL_PAGE_COUNT \
    (NAND_BLOCK_COUNT * NAND_PAGES_PER_BLOCK)

#define FTL_INVALID_PPN (-1)

typedef enum {
    FTL_OK = 0,
    FTL_ERROR_NULL_POINTER,
    FTL_ERROR_INVALID_LPN,
    FTL_ERROR_UNMAPPED_LPN,
    FTL_ERROR_ALREADY_WRITTEN,
    FTL_ERROR_NO_FREE_PAGE,
    FTL_ERROR_NAND
} FTLStatus;

typedef struct {
    NANDFlash nand;
    int l2p[FTL_LOGICAL_PAGE_COUNT];
    int next_free_ppn;
} FTL;

void ftl_init(FTL *ftl);
FTLStatus ftl_write(FTL *ftl, int lpn, uint32_t data);
FTLStatus ftl_read(const FTL *ftl, int lpn, uint32_t *data);

#endif