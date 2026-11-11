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

    if (ftl->next_free_ppn >= FTL_PHYSICAL_PAGE_COUNT) {
        return FTL_ERROR_NO_FREE_PAGE;
    }

    int old_ppn = ftl->l2p[lpn];
    int new_ppn = ftl->next_free_ppn;

    int new_block = new_ppn / NAND_PAGES_PER_BLOCK;
    int new_page = new_ppn % NAND_PAGES_PER_BLOCK;

    // Program the new physical page first
    if (nand_program(&ftl->nand, new_block, new_page, data)
        != NAND_OK) {
        return FTL_ERROR_NAND;
    }

    // Invalidate the previous physical page if one exists
    if (old_ppn != FTL_INVALID_PPN) {
        int old_block = old_ppn / NAND_PAGES_PER_BLOCK;
        int old_page = old_ppn % NAND_PAGES_PER_BLOCK;

        if (nand_invalidate(&ftl->nand, old_block, old_page)
            != NAND_OK) {

            // The new page is already programmed.
            // Mark it invalid and skip this consumed PPN.
            (void)nand_invalidate(&ftl->nand, new_block, new_page);
            ftl->next_free_ppn++;

            return FTL_ERROR_NAND;
        }
    }

    // Commit the new mapping
    ftl->l2p[lpn] = new_ppn;
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