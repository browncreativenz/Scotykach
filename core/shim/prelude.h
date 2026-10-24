/**
 * @file prelude.h
 * @brief Force-included ahead of every firmware source file.
 *
 * On the Daisy, <daisy.h> and the ARM toolchain pull in the C and C++
 * standard headers indirectly, and some firmware headers rely on that
 * (size_t in detector.h, std::sin in lutsinosc.h, uint32_t in synclock.h).
 * This header supplies them, plus libDaisy's float-printing macros that
 * expose.h uses, so the firmware compiles unmodified.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>

#ifndef FLT_FMT
#define FLT_FMT(prec)     "%.*f"
#define FLT_VAR(prec, x)  (int)(prec), (double)(x)
#define FLT_FMT3          FLT_FMT(3)
#define FLT_VAR3(x)       FLT_VAR(3, x)
#endif

/** Integer remainder as the Cortex-M7 computes it. Its divide instructions
 *  don't trap on a zero divisor (x / 0 is 0, so x % 0 is x), and some
 *  firmware paths rely on that; on x86 the same code would crash. Used by
 *  patches/portability. */
template <typename A, typename B>
constexpr auto arm_mod(A a, B b) -> decltype(a % b)
{
    return b == 0 ? a : a % b;
}

/** Float to integer conversion as the Cortex-M7's VCVT instructions do it:
 *  saturating, with NaN giving 0. In C++ an out-of-range conversion is
 *  undefined, and x86 gives different values (often the opposite branch),
 *  so firmware paths that convert inf or NaN need these. Used by
 *  patches/portability. */
inline uint32_t arm_f2u32(float x)
{
    if (!(x > 0.f)) return 0;                     // NaN, zero, negative
    if (x >= 4294967296.f) return 0xFFFFFFFFu;
    return (uint32_t)x;
}

inline int32_t arm_f2s32(float x)
{
    if (std::isnan(x)) return 0;
    if (x >= 2147483648.f) return 2147483647;
    if (x < -2147483648.f) return -2147483647 - 1;
    return (int32_t)x;
}
