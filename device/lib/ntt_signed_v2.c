// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

/**
@file ntt_signed_v2.c

ARMv8-A implementation (v2) of the signed lazy-reduction Montgomery NTT.

Algorithm is identical to cr_signed_ntt_lazy_inpl() in ntt.c; only the implementation differs:
  1. In-place: the ZZ (uint32_t) input array is used directly as int32_t, removing the
     16384-entry signed_vec stack buffer and the copy-in / copy-out loops.
     (coefficients < q < 2^27; lazy intermediates |x| < (log2(n) + 1) * q < 2^31)
  2. q and q' (= inv_q) are read once at function entry and passed in registers.
  3. Montgomery multiplication asm is non-volatile and has no "memory" clobber.
Unchanged from the original:
  - per group: s = mont(root, R2)            (Montgomery scaling of the twiddle)
  - butterfly without correction: x[k] = u + v, x[k+tt] = u - v
  - one final normalization: x = mont(x, R); x += (x >> 31) & q
  - 30-bit primes (q >= 0x20000000) and SE_NTT_OTF fall back to the original function.
*/

#include <stdint.h>

#include "defines.h"
#include "ntt.h"
#include "parameters.h"

// -- Original implementation (ntt.c), used as fallback
void cr_signed_ntt_lazy_inpl(const Parms *parms, const ZZ *ntt_roots, ZZ *vec);

#if defined(__aarch64__) && !defined(SE_PRIMESIZE_64)
/**
Signed Montgomery multiplication, R = 2^32. Bit-identical to cr_mont_mul_asm():
  z = (int64)a * b                  (SMULL)
  k = (int32)(z_lo32 * q' mod 2^32) (MUL, 32-bit; original uses the low word of a 64-bit MUL)
  r = z + (int64)k * q              (SMADDL, k and q sign-extended as in the original)
  return high 32 bits of r          (original: LSR #32 then truncation to int32)
*/
static inline int32_t mont_mul_v2(int32_t a, int32_t b, int32_t q, uint32_t qinv)
{
    int64_t z, r;
    int32_t k;
    __asm("smull  %x[z], %w[a], %w[b]\n\t"         // z = a*b
          "mul    %w[k], %w[z], %w[qinv]\n\t"      // k = z_lo * q'
          "smaddl %x[r], %w[k], %w[q], %x[z]\n\t"  // r = z + k*q
          : [z] "=&r"(z), [k] "=&r"(k), [r] "=&r"(r)
          : [a] "r"(a), [b] "r"(b), [q] "r"(q), [qinv] "r"(qinv));
    return (int32_t)(r >> 32);
}

void cr_signed_ntt_lazy_inpl_v2(const Parms *parms, const ZZ *ntt_roots, ZZ *vec)
{
    se_assert(parms && parms->curr_modulus && vec);
    const Modulus *mod = parms->curr_modulus;
#ifdef SE_NTT_OTF
    cr_signed_ntt_lazy_inpl(parms, ntt_roots, vec);
    return;
#else
    if (mod->value >= 0x20000000)  // 30-bit prime: original path
    {
        cr_signed_ntt_lazy_inpl(parms, ntt_roots, vec);
        return;
    }
    se_assert(ntt_roots);

    const size_t n      = parms->coeff_count;
    const size_t logn   = parms->logn;
    const int32_t q     = (int32_t)mod->value;
    const uint32_t qinv = (uint32_t)mod->inv_q;
    const int32_t R     = (int32_t)mod->R;
    const int32_t R2    = (int32_t)mod->R2;
    int32_t *x          = (int32_t *)vec;  // in-place signed view

    size_t h  = 1;
    size_t tt = n / 2;
    for (size_t i = 0; i < logn; i++, h *= 2, tt /= 2)  // rounds
    {
        for (size_t j = 0, kstart = 0; j < h; j++, kstart += 2 * tt)  // groups
        {
            const int32_t s = mont_mul_v2((int32_t)ntt_roots[h + j], R2, q, qinv);
            for (size_t k = kstart; k < (kstart + tt); k++)  // pairs
            {
                int32_t u = x[k];
                int32_t v = mont_mul_v2(x[k + tt], s, q, qinv);
                x[k]      = u + v;  // lazy: no correction
                x[k + tt] = u - v;
            }
        }
    }
    for (size_t i = 0; i < n; i++)
    {
        int32_t y = mont_mul_v2(x[i], R, q, qinv);  // (-q, q)
        y += (y >> 31) & q;
        x[i] = y;
    }
#endif
}
#else
// -- Non-AArch64 or 64-bit ZZ build: v2 is not available, use the original
void cr_signed_ntt_lazy_inpl_v2(const Parms *parms, const ZZ *ntt_roots, ZZ *vec)
{
    cr_signed_ntt_lazy_inpl(parms, ntt_roots, vec);
}
#endif
