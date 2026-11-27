#pragma once

#include <cmath>
#include <cstdio>

extern int g_checks;
extern int g_failures;

#define CHECK(cond)                                                                              \
    do {                                                                                         \
        ++g_checks;                                                                              \
        if (!(cond)) {                                                                           \
            ++g_failures;                                                                        \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                          \
        }                                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                    \
    do {                                                                                         \
        ++g_checks;                                                                              \
        const double _a = double(a);                                                             \
        const double _b = double(b);                                                             \
        if (std::fabs(_a - _b) > double(tol)) {                                                  \
            ++g_failures;                                                                        \
            std::printf("FAIL %s:%d: %s=%g vs %s=%g (tol %g)\n", __FILE__, __LINE__, #a, _a,    \
                        #b, _b, double(tol));                                                    \
        }                                                                                        \
    } while (0)
