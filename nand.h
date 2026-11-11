#ifndef NAND_H
#define NAND_H

#include <stdint.h>

#define NAND_BLOCK_COUNT 3
#define NAND_PAGES_PER_BLOCK 4
#define NAND_ERASED_DATA UINT32_MAX

typedef enum {
    NAND_PAGE_ERASED = 0,
    NAND_PAGE_PROGRAMMED,
    NAND_PAGE_INVALID
} NANDPageState;

typedef enum {
    NAND_OK = 0,
    NAND_ERROR_NULL_POINTER,
    NAND_ERROR_INVALID_ADDRESS,
    NAND_ERROR_PAGE_NOT_PROGRAMMED,
    NAND_ERROR_PAGE_ALREADY_PROGRAMMED,
    NAND_ERROR_PAGE_NOT_VALID
} NANDStatus;

typedef struct {
    uint32_t data;
    NANDPageState state;
} NANDPage;

typedef struct {
    NANDPage pages[NAND_PAGES_PER_BLOCK];
    uint32_t erase_count;
} NANDBlock;

typedef struct {
    NANDBlock blocks[NAND_BLOCK_COUNT];
} NANDFlash;

void nand_init(NANDFlash *nand);

NANDStatus nand_program(
    NANDFlash *nand,
    int block,
    int page,
    uint32_t data
);

NANDStatus nand_read(
    const NANDFlash *nand,
    int block,
    int page,
    uint32_t *data
);

NANDStatus nand_erase(NANDFlash *nand, int block);

NANDStatus nand_invalidate(
    NANDFlash *nand,
    int block,
    int page
);

void nand_print(const NANDFlash *nand);

#endif