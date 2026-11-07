#include "ftl.h"
#include <stddef.h>

static int is_valid_lpn(int lpn)
{
    return lpn >= 0 && lpn < FTL_LOGICAL_PAGE_COUNT;
}

void ftl_init(FTL *ftl)
{
    if (ftl == NULL) {
        return;
    }

    nand_init(&ftl->nand);

    for (int i = 0; i < FTL_LOGICAL_PAGE_COUNT; i++) {
        ftl->l2p[i] = FTL_INVALID_PPN;
    }

    ftl->next_free_ppn = 0;
}

FTLStatus ftl_write(FTL *ftl, int lpn, uint32_t data)
{
    if (ftl == NULL) {
        return FTL_ERROR_NULL_POINTER;
    }

    if (!is_valid_lpn(lpn)) {
        return FTL_ERROR_INVALID_LPN;
    }

    if (ftl->l2p[lpn] != FTL_INVALID_PPN) {
        return FTL_ERROR_ALREADY_WRITTEN;
    }

    if (ftl->next_free_ppn >= FTL_LOGICAL_PAGE_COUNT) {
        return FTL_ERROR_NO_FREE_PAGE;
    }

    int ppn = ftl->next_free_ppn;

    int block = ppn / NAND_PAGES_PER_BLOCK;
    int page = ppn % NAND_PAGES_PER_BLOCK;

    if (nand_program(&ftl->nand, block, page, data) != NAND_OK) {
        return FTL_ERROR_NAND;
    }

    ftl->l2p[lpn] = ppn;
    ftl->next_free_ppn++;

    return FTL_OK;
}

FTLStatus ftl_read(const FTL *ftl, int lpn, uint32_t *data)
{
    if (ftl == NULL || data == NULL) {
        return FTL_ERROR_NULL_POINTER;
    }

    if (!is_valid_lpn(lpn)) {
        return FTL_ERROR_INVALID_LPN;
    }

    int ppn = ftl->l2p[lpn];

    if (ppn == FTL_INVALID_PPN) {
        return FTL_ERROR_UNMAPPED_LPN;
    }

    int block = ppn / NAND_PAGES_PER_BLOCK;
    int page = ppn % NAND_PAGES_PER_BLOCK;

    if (nand_read(&ftl->nand, block, page, data) != NAND_OK) {
        return FTL_ERROR_NAND;
    }

    return FTL_OK;
}