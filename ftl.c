
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
    if (nand_program(&ftl->nand, new_block, new_page, data) != NAND_OK) {
        return FTL_ERROR_NAND;
    }

    // Invalidate the previous physical page if one exists
    if (old_ppn != FTL_INVALID_PPN) {
        int old_block = old_ppn / NAND_PAGES_PER_BLOCK;
        int old_page = old_ppn % NAND_PAGES_PER_BLOCK;

        if (nand_invalidate(&ftl->nand, old_block, old_page) != NAND_OK) {

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

FTLStatus ftl_gc_get_block_stats(const FTL *ftl, int block, FTLBlockStats *stats)
{
    if (ftl == NULL || stats == NULL) {
        return FTL_ERROR_NULL_POINTER;
    }

    if (block < 0 || block >= NAND_BLOCK_COUNT) {
        return FTL_ERROR_INVALID_BLOCK;
    }

    FTLBlockStats result = {0, 0, 0};

    for (int page = 0; page < NAND_PAGES_PER_BLOCK; page++) {
        const NANDPage *current = &ftl->nand.blocks[block].pages[page];

        if (current->state == NAND_PAGE_ERASED) {
            result.erased_pages++;
        } else if (current->state == NAND_PAGE_PROGRAMMED) {
            result.valid_pages++;
        } else if (current->state == NAND_PAGE_INVALID) {
            result.invalid_pages++;
        } else {
            return FTL_ERROR_NAND;
        }
    }

    *stats = result;
    return FTL_OK;
}

FTLStatus ftl_gc_select_victim(const FTL *ftl, int *victim_block)
{
    if (ftl == NULL || victim_block == NULL) {
        return FTL_ERROR_NULL_POINTER;
    }

    FTLBlockStats stats[NAND_BLOCK_COUNT];
    int total_erased = 0;

    // Step 1: Count all NAND block states.
    for (int block = 0; block < NAND_BLOCK_COUNT; block++) {
        FTLStatus status = ftl_gc_get_block_stats(ftl, block, &stats[block]);

        if (status != FTL_OK) {
            return status;
        }

        total_erased += stats[block].erased_pages;
    }

    int best_block = -1;
    int best_invalid = -1;
    int best_valid = NAND_PAGES_PER_BLOCK + 1;

    // Step 2: Find a victim with sufficient migration space.
    for (int block = 0; block < NAND_BLOCK_COUNT; block++) {
        int free_outside = total_erased - stats[block].erased_pages;

        if (stats[block].invalid_pages == 0 ||
            stats[block].valid_pages > free_outside) {
            continue;
        }

        if (stats[block].invalid_pages > best_invalid ||
            (stats[block].invalid_pages == best_invalid && stats[block].valid_pages < best_valid)) {

            best_block = block;
            best_invalid = stats[block].invalid_pages;
            best_valid = stats[block].valid_pages;
        }
    }

    if (best_block < 0) {
        return FTL_ERROR_NO_GC_VICTIM;
    }

    *victim_block = best_block;
    return FTL_OK;
}