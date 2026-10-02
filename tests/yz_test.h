// tests/yz_test.h —— 字帖主机测试共用的断言宏（不受 NDEBUG 影响）。
#pragma once

#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond)) {                                                            \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)
