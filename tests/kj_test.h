// tests/kj_test.h —— 限定猜拳主机测试共用的断言宏（不受 NDEBUG 影响）。
#pragma once

#include <stdio.h>
#include <stdlib.h>

static int kj_test_failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            kj_test_failures++;                                                    \
        }                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                             \
    do {                                                                           \
        long long _va = (long long)(a), _vb = (long long)(b);                      \
        if (_va != _vb) {                                                          \
            fprintf(stderr, "%s:%d: CHECK_EQ failed: %s (%lld) != %s (%lld)\n",     \
                    __FILE__, __LINE__, #a, _va, #b, _vb);                         \
            kj_test_failures++;                                                    \
        }                                                                          \
    } while (0)

#define KJ_TEST_DONE(name)                                                         \
    do {                                                                           \
        if (kj_test_failures) {                                                    \
            fprintf(stderr, "%s: %d failure(s)\n", name, kj_test_failures);        \
            return 1;                                                              \
        }                                                                          \
        printf("%s: PASS\n", name);                                                \
        return 0;                                                                  \
    } while (0)
