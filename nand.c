#include "nand.h"
#include <stdio.h>

static int is_valid_block(int block)
{
    return block >= 0 && block < NAND_BLOCK_COUNT;
}

static int is_valid_page(int page)
{
    return page >= 0 && page < NAND_PAGES_PER_BLOCK;
}

void nand_init(NANDFlash *nand)
{
    if (nand == NULL) {
        return;
    }

    for (int b = 0; b < NAND_BLOCK_COUNT; b++) {
        nand->blocks[b].erase_count = 0;
        for (int p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
            nand->blocks[b].pages[p].data = NAND_ERASED_DATA;
            nand->blocks[b].pages[p].state = NAND_PAGE_ERASED;
        }
    }
}

NANDStatus nand_program(NANDFlash *nand, int block, int page, uint32_t data)
{
    if (nand == NULL) {
        return NAND_ERROR_NULL_POINTER;
    }
    if (!is_valid_block(block) || !is_valid_page(page)) {
        return NAND_ERROR_INVALID_ADDRESS;
    }

    NANDPage *target = &nand->blocks[block].pages[page];
    if (target->state != NAND_PAGE_ERASED) {
        return NAND_ERROR_PAGE_ALREADY_PROGRAMMED;
    }

    target->data = data; // == nand->blocks[a].pages[b].data = data
    target->state = NAND_PAGE_PROGRAMMED; // Page is used !!!
    return NAND_OK;
}

NANDStatus nand_read(const NANDFlash *nand, int block, int page, uint32_t *data)
{
    if (nand == NULL || data == NULL) {
        return NAND_ERROR_NULL_POINTER;
    }
    if (!is_valid_block(block) || !is_valid_page(page)) {
        return NAND_ERROR_INVALID_ADDRESS;
    }

    const NANDPage *target = &nand->blocks[block].pages[page]; // Take address
    if (target->state != NAND_PAGE_PROGRAMMED) {
        return NAND_ERROR_PAGE_NOT_PROGRAMMED;
    }

    *data = target->data; // Use the value of address as the value pointed to by *data. !!!
    return NAND_OK;
}

NANDStatus nand_erase(NANDFlash *nand, int block)
{
    if (nand == NULL) {
        return NAND_ERROR_NULL_POINTER;
    }
    if (!is_valid_block(block)) {
        return NAND_ERROR_INVALID_ADDRESS;
    }

    NANDBlock *target = &nand->blocks[block];
    for (int p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
        target->pages[p].data = NAND_ERASED_DATA;
        target->pages[p].state = NAND_PAGE_ERASED;
    }
    target->erase_count++;
    return NAND_OK;
}

NANDStatus nand_invalidate(NANDFlash *nand, int block, int page)
{
    if (nand == NULL) {
        return NAND_ERROR_NULL_POINTER;
    }

    if (!is_valid_block(block) || !is_valid_page(page)) {
        return NAND_ERROR_INVALID_ADDRESS;
    }

    NANDPage *target = &nand->blocks[block].pages[page];

    if (target->state != NAND_PAGE_PROGRAMMED) {
        return NAND_ERROR_PAGE_NOT_VALID;
    }

    target->state = NAND_PAGE_INVALID;

    return NAND_OK;
}

void nand_print(const NANDFlash *nand)
{
    if (nand == NULL) {
        return;
    }

    printf("\n===== NAND Flash State =====\n");
    for (int b = 0; b < NAND_BLOCK_COUNT; b++) {
        printf("Block %d (erase_count=%u)\n", b,
               (unsigned)nand->blocks[b].erase_count);
        for (int p = 0; p < NAND_PAGES_PER_BLOCK; p++) {
            const NANDPage *page = &nand->blocks[b].pages[p];
            if (page->state == NAND_PAGE_ERASED) {
                printf("  Page %d: ERASED\n", p);

            } else if (page->state == NAND_PAGE_PROGRAMMED) {
                printf("  Page %d: PROGRAMMED, data=%u\n",
                    p, (unsigned)page->data);

            } else if (page->state == NAND_PAGE_INVALID) {
                printf("  Page %d: INVALID, old_data=%u\n",
                    p, (unsigned)page->data);
            }
        }
    }
    printf("============================\n");
}