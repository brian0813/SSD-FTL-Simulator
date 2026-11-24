#include "tests/test_suite.h"
#include <stdio.h>

int main(void)
{
    run_nand_tests();
    run_ftl_tests();
    run_gc_tests();
    run_wl_tests();

    printf("\nAll Stage tests passed!\n");
    return 0;
}
