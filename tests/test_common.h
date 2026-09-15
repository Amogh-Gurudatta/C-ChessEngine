#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>

extern int testsRun;
extern int testsFailed;

#define CHECK(cond, desc)                                              \
    do                                                                  \
    {                                                                    \
        testsRun++;                                                      \
        if (!(cond))                                                      \
        {                                                                  \
            testsFailed++;                                                 \
            printf("  FAIL: %s (%s:%d)\n", desc, __FILE__, __LINE__);      \
        }                                                                  \
    } while (0)

#define SECTION(name) printf("-- %s --\n", name)

#endif // TEST_COMMON_H
