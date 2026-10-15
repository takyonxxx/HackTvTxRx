#ifndef SIMDDOT_H
#define SIMDDOT_H

#include <cstddef>
#include <cmath>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define HTS_HAVE_SSE2 1
#endif

/*
 * dotf(): sum(a[i] * b[i]) for i in [0, n), single precision.
 *
 * Four independent partial sums so the compiler is not asked to reassociate
 * a float reduction (which neither GCC nor MSVC will do without fast-math).
 * SSE2 is baseline on x86-64, so this is the same code path on MinGW and
 * MSVC; the scalar fallback keeps the identical 4-lane summation order.
 *
 * Neither pointer needs to be aligned.
 */
inline float dotf(const float* a, const float* b, int n)
{
    float sum = 0.0f;
    int i = 0;

#ifdef HTS_HAVE_SSE2
    __m128 acc0 = _mm_setzero_ps();
    __m128 acc1 = _mm_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        acc0 = _mm_add_ps(acc0, _mm_mul_ps(_mm_loadu_ps(a + i),     _mm_loadu_ps(b + i)));
        acc1 = _mm_add_ps(acc1, _mm_mul_ps(_mm_loadu_ps(a + i + 4), _mm_loadu_ps(b + i + 4)));
    }
    for (; i + 4 <= n; i += 4)
        acc0 = _mm_add_ps(acc0, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
    acc0 = _mm_add_ps(acc0, acc1);
    // horizontal add
    __m128 shuf = _mm_shuffle_ps(acc0, acc0, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 sums = _mm_add_ps(acc0, shuf);
    shuf = _mm_movehl_ps(shuf, sums);
    sums = _mm_add_ss(sums, shuf);
    sum = _mm_cvtss_f32(sums);
#else
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    sum = (s0 + s1) + (s2 + s3);
#endif

    for (; i < n; ++i)
        sum += a[i] * b[i];
    return sum;
}

/* y[i] += a * x[i] for i in [0, n). Used for block FIRs: one tap at a time
 * across the whole block, so there is no reduction and the loop is 4-wide. */
inline void axpyf(float* y, const float* x, float a, int n)
{
    int i = 0;
#ifdef HTS_HAVE_SSE2
    const __m128 va = _mm_set1_ps(a);
    for (; i + 8 <= n; i += 8) {
        _mm_storeu_ps(y + i,     _mm_add_ps(_mm_loadu_ps(y + i),     _mm_mul_ps(va, _mm_loadu_ps(x + i))));
        _mm_storeu_ps(y + i + 4, _mm_add_ps(_mm_loadu_ps(y + i + 4), _mm_mul_ps(va, _mm_loadu_ps(x + i + 4))));
    }
    for (; i + 4 <= n; i += 4)
        _mm_storeu_ps(y + i, _mm_add_ps(_mm_loadu_ps(y + i), _mm_mul_ps(va, _mm_loadu_ps(x + i))));
#endif
    for (; i < n; ++i)
        y[i] += a * x[i];
}

/* out[i] = sqrt(re[i]^2 + im[i]^2) */
inline void vecMagnitude(const float* re, const float* im, float* out, int n)
{
    int i = 0;
#ifdef HTS_HAVE_SSE2
    for (; i + 4 <= n; i += 4) {
        const __m128 r = _mm_loadu_ps(re + i);
        const __m128 m = _mm_loadu_ps(im + i);
        _mm_storeu_ps(out + i, _mm_sqrt_ps(_mm_add_ps(_mm_mul_ps(r, r), _mm_mul_ps(m, m))));
    }
#endif
    for (; i < n; ++i)
        out[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]);
}

#endif // SIMDDOT_H
