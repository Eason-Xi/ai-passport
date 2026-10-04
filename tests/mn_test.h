// tests/mn_test.h —— 节拍器主机测试共用的断言宏（不受 NDEBUG 影响）。
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

#define CHECK_NEAR(a, b, tol)                                                     \
    do {                                                                          \
        long long _a = (long long)(a), _b = (long long)(b);                       \
        long long _d = _a > _b ? _a - _b : _b - _a;                               \
        if (_d > (long long)(tol)) {                                              \
            fprintf(stderr, "%s:%d: CHECK_NEAR failed: %s=%lld %s=%lld tol=%lld\n",  \
                    __FILE__, __LINE__, #a, _a, #b, _b, (long long)(tol));        \
            exit(1);                                                              \
        }                                                                         \
    } while (0)
