// Bit-exact equivalence test: cr_signed_ntt_lazy_inpl_v2 vs original cr_signed_ntt_lazy_inpl.
// Also checks mont_mul (original cr_mont_mul_asm via mul_mod_mont) vs the v2 asm on random/boundary operands.
// Link against libseal_embedded.a (Release -O3). Output: one CSV line per (N, q) + summary.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "modulus.h"
#include "ntt.h"
#include "parameters.h"
#include "uintmodarith.h"

void cr_signed_ntt_lazy_inpl(const Parms *parms, const ZZ *ntt_roots, ZZ *vec);
void cr_signed_ntt_lazy_inpl_v2(const Parms *parms, const ZZ *ntt_roots, ZZ *vec);

static uint64_t rs;
static uint64_t splitmix(void)
{
    uint64_t z = (rs += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static uint32_t powmod(uint64_t b, uint64_t e, uint64_t q)
{
    uint64_t r = 1;
    b %= q;
    while (e) { if (e & 1) r = r * b % q; b = b * b % q; e >>= 1; }
    return (uint32_t)r;
}
static size_t bitrev_(size_t x, size_t logn)
{
    size_t r = 0;
    for (size_t i = 0; i < logn; i++) { r = (r << 1) | (x & 1); x >>= 1; }
    return r;
}
// same asm as ntt_signed_v2.c (static there), duplicated here only to test the multiplier alone
static inline int32_t mont_mul_v2(int32_t a, int32_t b, int32_t q, uint32_t qinv)
{
    int64_t z, r; int32_t k;
    __asm("smull  %x[z], %w[a], %w[b]\n\t"
          "mul    %w[k], %w[z], %w[qinv]\n\t"
          "smaddl %x[r], %w[k], %w[q], %x[z]\n\t"
          : [z] "=&r"(z), [k] "=&r"(k), [r] "=&r"(r)
          : [a] "r"(a), [b] "r"(b), [q] "r"(q), [qinv] "r"(qinv));
    return (int32_t)(r >> 32);
}

int main(int argc, char **argv)
{
    uint64_t seed = argc > 1 ? strtoull(argv[1], 0, 0) : 0x5EA1E2026ULL;
    int nrand     = argc > 2 ? atoi(argv[2]) : 1000;
    rs            = seed;
    const uint32_t primes[3] = {134012929, 134111233, 134176769};
    // psi for N=4096 from get_ntt_root() table (ntt.c)
    const uint32_t psi4096[3] = {7470, 3856, 24149};
    const size_t Ns[3] = {1024, 2048, 4096};
    long total_cases = 0, total_mismatch = 0;

    // ---- 1) multiplier-level check ----
    long mm_cases = 0, mm_bad = 0;
    for (int pi = 0; pi < 3; pi++)
    {
        Modulus m; memset(&m, 0, sizeof m); set_modulus(primes[pi], &m);
        int32_t q = (int32_t)m.value;
        int32_t edge[] = {0, 1, -1, q - 1, -(q - 1), q, -q, (int32_t)m.R, (int32_t)m.R2,
                          13 * q, -13 * q, INT32_MAX, INT32_MIN + 1, 1 << 30, -(1 << 30)};
        int ne = sizeof edge / sizeof edge[0];
        for (int i = 0; i < ne; i++)
            for (int j = 0; j < ne; j++)
            {
                // the NTT only ever multiplies by a twiddle in (-q, q); still test all pairs
                ZZsign o = mul_mod_mont((ZZ)edge[i], (ZZ)edge[j], &m);
                int32_t v = mont_mul_v2(edge[i], edge[j], q, (uint32_t)m.inv_q);
                mm_cases++; if ((int32_t)o != v) mm_bad++;
            }
        for (int t = 0; t < 1000000; t++)
        {
            int32_t a = (int32_t)splitmix();                       // full int32 range
            int32_t b = (int32_t)(splitmix() % (2 * (uint32_t)q - 1)) - (q - 1);  // (-q, q)
            ZZsign o = mul_mod_mont((ZZ)a, (ZZ)b, &m);
            int32_t v = mont_mul_v2(a, b, q, (uint32_t)m.inv_q);
            mm_cases++; if ((int32_t)o != v) mm_bad++;
        }
    }
    printf("MONTMUL,cases=%ld,mismatch=%ld\n", mm_cases, mm_bad);

    // ---- 2) full NTT check ----
    printf("N,q,psi,psi_source,psi^N==-1,random_cases,boundary_cases,mismatch_cases,first_mismatch\n");
    for (int ni = 0; ni < 3; ni++)
        for (int pi = 0; pi < 3; pi++)
        {
            size_t n = Ns[ni], logn = 0;
            while (((size_t)1 << logn) < n) logn++;
            uint32_t q = primes[pi];
            Modulus m; memset(&m, 0, sizeof m); set_modulus(q, &m);
            Parms p; memset(&p, 0, sizeof p);
            p.coeff_count = n; p.logn = logn; p.curr_modulus = &m;
            uint32_t psi; const char *src;
            if (n == 4096) { psi = psi4096[pi]; src = "get_ntt_root"; }
            else if (q == 134012929) { psi = (n == 1024) ? 142143 : 85250; src = "get_ntt_root"; }
            else { psi = powmod(psi4096[pi], 4096 / n, q); src = "psi4096^(4096/N)"; }
            int neg1 = powmod(psi, n, q) == q - 1;
            ZZ *roots = calloc(n, sizeof(ZZ));
            roots[0] = 1;
            uint64_t pw = psi;
            for (size_t i = 1; i < n; i++) { roots[bitrev_(i, logn)] = (ZZ)pw; pw = pw * psi % q; }

            ZZ *in = malloc(n * sizeof(ZZ)), *a = malloc(n * sizeof(ZZ)), *b = malloc(n * sizeof(ZZ));
            long cases = 0, bad = 0, nb = 0; char first[128] = "-";
            int nboundary = 9;
            for (int t = 0; t < nrand + nboundary; t++)
            {
                if (t < nboundary)
                {
                    for (size_t i = 0; i < n; i++)
                    {
                        switch (t)
                        {
                            case 0: in[i] = 0; break;
                            case 1: in[i] = q - 1; break;
                            case 2: in[i] = 1; break;
                            case 3: in[i] = (i & 1) ? q - 1 : 0; break;
                            case 4: in[i] = (i == 0) ? 1 : 0; break;
                            case 5: in[i] = (i == n - 1) ? q - 1 : 0; break;
                            case 6: in[i] = (ZZ)(i % q); break;
                            case 7: in[i] = (i & 1) ? 0 : q - 1; break;
                            case 8: in[i] = (q - 1) / 2 + (i & 1); break;
                        }
                    }
                    nb++;
                }
                else
                    for (size_t i = 0; i < n; i++) in[i] = (ZZ)(splitmix() % q);
                memcpy(a, in, n * sizeof(ZZ)); memcpy(b, in, n * sizeof(ZZ));
                cr_signed_ntt_lazy_inpl(&p, roots, a);
                cr_signed_ntt_lazy_inpl_v2(&p, roots, b);
                cases++;
                if (memcmp(a, b, n * sizeof(ZZ)))
                {
                    if (!bad)
                        for (size_t i = 0; i < n; i++)
                            if (a[i] != b[i]) { snprintf(first, sizeof first, "case%d_idx%zu_orig%u_v2%u", t, i, a[i], b[i]); break; }
                    bad++;
                }
                // sanity: outputs fully reduced
                for (size_t i = 0; i < n; i++) if (b[i] >= q) { bad++; snprintf(first, sizeof first, "unreduced"); break; }
            }
            printf("%zu,%u,%u,%s,%d,%ld,%ld,%ld,%s\n", n, q, psi, src, neg1, cases - nb, nb, bad, first);
            total_cases += cases; total_mismatch += bad;
            free(roots); free(in); free(a); free(b);
        }
    printf("TOTAL,cases=%ld,mismatch=%ld,seed=0x%llx\n", total_cases, total_mismatch, (unsigned long long)seed);
    return (total_mismatch || mm_bad) ? 1 : 0;
}
