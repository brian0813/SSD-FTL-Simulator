#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <assert.h>
#include <stdio.h>

#define RUN_TEST(test_func)                  \
    do {                                     \
        test_func();                         \
        printf("[ PASS ] %s\n", #test_func); \
    } while (0)

#endif
