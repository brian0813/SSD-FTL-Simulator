#include "ftl.h"
#include <stddef.h>

static int is_valid_lpn(int lpn)
{
    return lpn >= 0 && lpn < FTL_LOGICAL_PAGE_COUNT;
}

static FTLStatus validate_mapping(const FTL *ftl)
{
    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        int ppn = ftl->l2p[lpn];

        if (ppn == FTL_INVALID_PPN) {
            continue;
        }

        if (ppn < 0 || ppn >= FTL_PHYSICAL_PAGE_COUNT) {
            return FTL_ERROR_NAND;
        }

        int b = ppn / NAND_PAGES_PER_BLOCK;
        int p = ppn % NAND_PAGES_PER_BLOCK;

        if (ftl->nand.blocks[b].pages[p].state != NAND_PAGE_PROGRAMMED) {
            return FTL_ERROR_NAND;
        }
    }

    for (int ppn = 0; ppn < FTL_PHYSICAL_PAGE_COUNT; ppn++) {
        int b = ppn / NAND_PAGES_PER_BLOCK;
        int p = ppn % NAND_PAGES_PER_BLOCK;

        NANDPageState state = ftl->nand.blocks[b].pages[p].state;
        int owners = 0;

        for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
            if (ftl->l2p[lpn] == ppn) {
                owners++;
            }
        }

        if (state == NAND_PAGE_PROGRAMMED) {
            if (owners != 1) {
                return FTL_ERROR_NAND;
            }
        } else if (state == NAND_PAGE_ERASED ||
                   state == NAND_PAGE_INVALID) {
            if (owners != 0) {
                return FTL_ERROR_NAND;
            }
        } else {
            return FTL_ERROR_NAND;
        }
    }

    return FTL_OK;
}

static int find_erased_ppn(const FTL *ftl, int excluded_block)
{
    for (int offset = 0; offset < FTL_PHYSICAL_PAGE_COUNT; offset++) {
        int candidate = (ftl->next_free_ppn + offset) % FTL_PHYSICAL_PAGE_COUNT;

        int block = candidate / NAND_PAGES_PER_BLOCK;
        int page = candidate % NAND_PAGES_PER_BLOCK;

        if (block != excluded_block &&
            ftl->nand.blocks[block].pages[page].state == NAND_PAGE_ERASED) {
            return candidate;
        }
    }

    return FTL_INVALID_PPN;
}

static int count_erased_pages(const FTL *ftl)
{
    int total = 0;

    for (int block = 0; block < NAND_BLOCK_COUNT; block++) {
        for (int page = 0; page < NAND_PAGES_PER_BLOCK; page++) {
            if (ftl->nand.blocks[block].pages[page].state == NAND_PAGE_ERASED) {
                total++;
            }
        }
    }

    return total;
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

    if (ftl->next_free_ppn < 0 ||
        ftl->next_free_ppn >= FTL_PHYSICAL_PAGE_COUNT) {
        return FTL_ERROR_NAND;
    }

    if (validate_mapping(ftl) != FTL_OK) {
        return FTL_ERROR_NAND;
    }

    // Trigger GC early when erased pages are running low.
    if (count_erased_pages(ftl) <= FTL_GC_LOW_WATERMARK) {
        FTLStatus gc_status = ftl_gc_collect(ftl);

        if (gc_status != FTL_OK &&
            gc_status != FTL_ERROR_NO_GC_VICTIM) {
            return gc_status;
        }
    }

    // Find an actual erased physical page.
    int new_ppn = find_erased_ppn(ftl, -1);

    if (new_ppn == FTL_INVALID_PPN) {
        return FTL_ERROR_NO_FREE_PAGE;
    }

    int old_ppn = ftl->l2p[lpn];
    int new_block = new_ppn / NAND_PAGES_PER_BLOCK;
    int new_page = new_ppn % NAND_PAGES_PER_BLOCK;

    if (nand_program(&ftl->nand, new_block, new_page, data)
        != NAND_OK) {
        return FTL_ERROR_NAND;
    }

    if (old_ppn != FTL_INVALID_PPN) {
        int old_block = old_ppn / NAND_PAGES_PER_BLOCK;
        int old_page = old_ppn % NAND_PAGES_PER_BLOCK;

        if (nand_invalidate(&ftl->nand, old_block, old_page)
            != NAND_OK) {

            (void)nand_invalidate(&ftl->nand, new_block, new_page);

            ftl->next_free_ppn =
                (new_ppn + 1) % FTL_PHYSICAL_PAGE_COUNT;

            return FTL_ERROR_NAND;
        }
    }

    ftl->l2p[lpn] = new_ppn;

    ftl->next_free_ppn = (new_ppn + 1) % FTL_PHYSICAL_PAGE_COUNT;

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

    if (ppn < 0 || ppn >= FTL_PHYSICAL_PAGE_COUNT) {
    return FTL_ERROR_NAND;
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

        if (stats[block].invalid_pages == 0 || stats[block].valid_pages > free_outside) {
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

FTLStatus ftl_gc_migrate_valid_pages(FTL *ftl, int victim_block)
{
    // 1. Validate arguments.
    if (ftl == NULL) {
        return FTL_ERROR_NULL_POINTER;
    }
    if (victim_block < 0 || victim_block >= NAND_BLOCK_COUNT) {
        return FTL_ERROR_INVALID_BLOCK;
    }
    if (ftl->next_free_ppn < 0 || ftl->next_free_ppn > FTL_PHYSICAL_PAGE_COUNT) {
        return FTL_ERROR_NAND;
    }

    // 2. Check victim block statistics.
    FTLBlockStats stats;
    FTLStatus status = ftl_gc_get_block_stats(ftl, victim_block, &stats);

    if (status != FTL_OK) {
        return status;
    }
    if (stats.invalid_pages == 0) {
        return FTL_ERROR_NO_GC_VICTIM;
    }

    // 3. Check all L2P mappings.
    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        int ppn = ftl->l2p[lpn];

        if (ppn == FTL_INVALID_PPN) {
            continue;
        }
        if (ppn < 0 || ppn >= FTL_PHYSICAL_PAGE_COUNT) {
            return FTL_ERROR_NAND;
        }

        int block = ppn / NAND_PAGES_PER_BLOCK;
        int page = ppn % NAND_PAGES_PER_BLOCK;

        if (ftl->nand.blocks[block].pages[page].state != NAND_PAGE_PROGRAMMED) {
            return FTL_ERROR_NAND;
        }
    }

    // 4. Check valid page ownership.
    for (int page = 0; page < NAND_PAGES_PER_BLOCK; page++) {
        if (ftl->nand.blocks[victim_block].pages[page].state != NAND_PAGE_PROGRAMMED) {
            continue;
        }

        int old_ppn = victim_block * NAND_PAGES_PER_BLOCK + page;

        int owners = 0;
        for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
            if (ftl->l2p[lpn] == old_ppn) {
                owners++;
            }
        }
        if (owners != 1) {
            return FTL_ERROR_NAND;
        }
    }

    // 5. Count available erased pages outside the victim.
    int free_outside = 0;

    for (int ppn = 0; ppn < FTL_PHYSICAL_PAGE_COUNT; ppn++) {
        int block = ppn / NAND_PAGES_PER_BLOCK;
        int page = ppn % NAND_PAGES_PER_BLOCK;

        if (block != victim_block && ftl->nand.blocks[block].pages[page].state == NAND_PAGE_ERASED) {
            free_outside++;
        }
    }

    if (free_outside < stats.valid_pages) {
        return FTL_ERROR_NO_FREE_PAGE;
    }

    // 6. Migrate each valid page.
    for (int page = 0; page < NAND_PAGES_PER_BLOCK; page++) {
        if (ftl->nand.blocks[victim_block].pages[page].state != NAND_PAGE_PROGRAMMED) {
            continue;
        }

        int old_ppn = victim_block * NAND_PAGES_PER_BLOCK + page;

        // Find the logical page owning old_ppn.
        int owner_lpn = FTL_INVALID_PPN;

        for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
            if (ftl->l2p[lpn] == old_ppn) {
                owner_lpn = lpn;
                break;
            }
        }

        // Read the original data.
        uint32_t value = 0;

        if (nand_read(&ftl->nand, victim_block, page, &value) != NAND_OK) {
            return FTL_ERROR_NAND;
        }

        // Find an erased page outside the victim.
        int new_ppn = find_erased_ppn(ftl, victim_block);

        if (new_ppn == FTL_INVALID_PPN) {
            return FTL_ERROR_NO_FREE_PAGE;
        }

        if (new_ppn == FTL_INVALID_PPN) {
            return FTL_ERROR_NO_FREE_PAGE;
        }

        int new_block = new_ppn / NAND_PAGES_PER_BLOCK;
        int new_page = new_ppn % NAND_PAGES_PER_BLOCK;

        // Program the new physical page first.
        if (nand_program(&ftl->nand, new_block, new_page, value) != NAND_OK) {
            return FTL_ERROR_NAND;
        }

        // Invalidate the original page.
        if (nand_invalidate(&ftl->nand, victim_block, page) != NAND_OK) {

            // Discard the newly programmed page on failure.
            (void)nand_invalidate(&ftl->nand, new_block, new_page);

            ftl->next_free_ppn = (new_ppn + 1) % FTL_PHYSICAL_PAGE_COUNT;
            return FTL_ERROR_NAND;
        }

        // Commit the new mapping.
        ftl->l2p[owner_lpn] = new_ppn;

        ftl->next_free_ppn = (new_ppn + 1) % FTL_PHYSICAL_PAGE_COUNT;
    }

    return FTL_OK;
}


FTLStatus ftl_gc_erase_victim(FTL *ftl, int victim_block)
{
    // 1. Validate arguments.
    if (ftl == NULL) {
        return FTL_ERROR_NULL_POINTER;
    }

    if (victim_block < 0 || victim_block >= NAND_BLOCK_COUNT) {
        return FTL_ERROR_INVALID_BLOCK;
    }

    // 2. Get victim block statistics.
    FTLBlockStats stats;

    FTLStatus status =
        ftl_gc_get_block_stats(ftl, victim_block, &stats);

    if (status != FTL_OK) {
        return status;
    }

    // 3. The block must contain reclaimable invalid pages.
    if (stats.invalid_pages == 0) {
        return FTL_ERROR_NO_GC_VICTIM;
    }

    // 4. Never erase a block containing valid data.
    if (stats.valid_pages > 0) {
        return FTL_ERROR_GC_BLOCK_NOT_READY;
    }

    // 5. Make sure no LPN still points into the victim.
    for (int lpn = 0; lpn < FTL_LOGICAL_PAGE_COUNT; lpn++) {
        int ppn = ftl->l2p[lpn];

        if (ppn == FTL_INVALID_PPN) {
            continue;
        }

        if (ppn < 0 || ppn >= FTL_PHYSICAL_PAGE_COUNT) {
            return FTL_ERROR_NAND;
        }

        int mapped_block = ppn / NAND_PAGES_PER_BLOCK;

        if (mapped_block == victim_block) {
            return FTL_ERROR_GC_BLOCK_NOT_READY;
        }
    }

    // 6. Erase the entire victim block.
    if (nand_erase(&ftl->nand, victim_block) != NAND_OK) {
        return FTL_ERROR_NAND;
    }

    return FTL_OK;
}

FTLStatus ftl_gc_collect(FTL *ftl)
{
    if (ftl == NULL) {
        return FTL_ERROR_NULL_POINTER;
    }

    if (ftl->next_free_ppn < 0 ||
        ftl->next_free_ppn >= FTL_PHYSICAL_PAGE_COUNT ||
        validate_mapping(ftl) != FTL_OK) {
        return FTL_ERROR_NAND;
    }

    int victim_block = -1;

    // Step 1：選擇 Victim Block
    FTLStatus status = ftl_gc_select_victim(ftl, &victim_block);

    // 如果選擇失敗，立即結束 GC 並回傳錯誤
    if (status != FTL_OK) {
        return status;
    }

    // Step 2：搬移 Victim Block 內的 Valid Pages
    status = ftl_gc_migrate_valid_pages(ftl, victim_block);

    // 如果搬移失敗，立即結束 GC，不能繼續擦除
    if (status != FTL_OK) {
        return status;
    }

    // Step 3：擦除 Victim Block，並將擦除結果回傳給呼叫者
    return ftl_gc_erase_victim(ftl, victim_block);
}