#!/usr/bin/env python3
"""Generate per-configuration source trees (outside the repo) from a pristine copy.

Configuration switching reproduces the existing method: toggling the commented lines in
  - lib/ntt.c        ntt_inpl():  ntt_non_lazy_inpl  <->  cr_signed_ntt_lazy_inpl
  - lib/uintmodarith.h mul_mod(): original Barrett reduce  <->  cr_barrett_mul_asm
  - test/main.c      n / nprimes and sym / asym test call
Variants:
  meas    : test harness exactly as in repo (5 warm-up + 1000 timed calls, prints sum/1000)
  csv     : same as meas, plus stores each iteration's (end-start) outside the timed region
            and prints them after the loop
  verify  : upstream-style correctness: 1 call per prime, check_decode_decrypt_inpl enabled,
            compare_poly(pte_calc, decrypted) and se_assert(!err) enabled
  verifyloop: like verify but runs the same 5 + 1000 calls as measurement, then checks the
            output of the last call
"""
import os, re, shutil, sys

def sub1(text, old, new, fname):
    c = text.count(old)
    if c != 1:
        sys.exit(f"[{fname}] expected exactly 1 match, got {c}: {old[:80]!r}")
    return text.replace(old, new)

def rw(path, fn):
    with open(path, encoding="utf-8") as f:
        t = f.read()
    t2 = fn(t)
    with open(path, "w", encoding="utf-8") as f:
        f.write(t2)

CONFIGS = {
    "baseline":   dict(lazy=False, barrett=False),
    "barrettmul": dict(lazy=False, barrett=True),
    "montlazy":   dict(lazy=True,  barrett=False),
    "integrated": dict(lazy=True,  barrett=True),
}
NP = {1024: 1, 2048: 1, 4096: 3}

def apply_config(dev, cfg):
    if cfg["lazy"]:
        def f(t):
            t = sub1(t, "    ntt_non_lazy_inpl(parms, ntt_roots, vec);\n    // ntt_non_lazy_inpl_test(",
                        "    // ntt_non_lazy_inpl(parms, ntt_roots, vec);\n    // ntt_non_lazy_inpl_test(", "ntt.c")
            t = sub1(t, "    // cr_signed_ntt_lazy_inpl(parms, ntt_roots, vec);\n#endif",
                        "    cr_signed_ntt_lazy_inpl(parms, ntt_roots, vec);\n#endif", "ntt.c")
            return t
        rw(f"{dev}/lib/ntt.c", f)
    if cfg["barrett"]:
        def f(t):
            t = sub1(t,
                "    ZZ product[2];\n    mul_uint_wide(op1, op2, product);\n    return barrett_reduce_wide(product, q);\n\n    //! our\n    // return cr_barrett_mul(op1, op2, q);\n    // return cr_barrett_mul_asm(op1, op2, q);",
                "    // ZZ product[2];\n    // mul_uint_wide(op1, op2, product);\n    // return barrett_reduce_wide(product, q);\n\n    //! our\n    // return cr_barrett_mul(op1, op2, q);\n    return cr_barrett_mul_asm(op1, op2, q);",
                "uintmodarith.h")
            return t
        rw(f"{dev}/lib/uintmodarith.h", f)

def apply_main(dev, mode, n):
    def f(t):
        t = sub1(t, "    const size_t n = 4096, nprimes = 3;",
                    f"    const size_t n = {n}, nprimes = {NP[n]};", "main.c")
        if mode == "sym":
            t = sub1(t, "    // test_ckks_encode_encrypt_sym(n, nprimes);\n    test_ckks_encode_encrypt_asym(n, nprimes);",
                        "    test_ckks_encode_encrypt_sym(n, nprimes);\n    // test_ckks_encode_encrypt_asym(n, nprimes);", "main.c")
        return t
    rw(f"{dev}/test/main.c", f)

TIMED_RE = re.compile(r"( *)// ! performance\n.*?Cycles:\)\) %llu\\n\\n\", \(unsigned long long\)sum/1000\);\n", re.S)

def apply_csv(dev):
    for fn in ("ckks_tests_sym.c", "ckks_tests_asym.c"):
        def f(t, fn=fn):
            t = sub1(t, '#include "m1cycles.h"\n',
                        '#include "m1cycles.h"\nstatic long long se_iter_cycles[1000];\n', fn)
            t = sub1(t, "                sum += (end - start);    \n",
                        "                sum += (end - start);    \n                se_iter_cycles[j] = (end - start);\n", fn)
            m = re.search(r'( *)printf\("ckks_encdode_encrypt_(a?sym) Cycles:\)\) %llu\\n\\n", \(unsigned long long\)sum/1000\);\n', t)
            assert m, fn
            ind = m.group(1)
            add = (f'{ind}for (int jj = 0; jj < 1000; jj++) printf("CYC,%zu,%zu,%d,%lld\\n", testnum, i, jj, se_iter_cycles[jj]);\n')
            t = t[:m.end()] + add + t[m.end():]
            return t
        rw(f"{dev}/test/{fn}", f)

def apply_verify(dev, loop):
    for fn, call_re in (("ckks_tests_sym.c", r"ckks_encode_encrypt_sym\(&parms, conj_vals_int, NULL, &shareable_prng, s, ntt_pte,\s*ntt_roots, c0, c1, s_test_save, c1_test_save\);"),
                        ("ckks_tests_asym.c", r"ckks_encode_encrypt_asym\(&parms, conj_vals_int, u, e1, ntt_roots, ntt_u_e1_pte,\s*ntt_u_save, ntt_e1_save, pk_c0, pk_c1\);")):
        def f(t, fn=fn, call_re=call_re):
            m = TIMED_RE.search(t)
            assert m, fn
            block = m.group(0)
            call = re.search(call_re, block).group(0)
            ind = m.group(1)
            if loop:
                # keep the exact measurement sequence, just drop the printf
                new = block
            else:
                new = f"{ind}// VERIFY: single call (upstream test semantics)\n{ind}{call}\n"
            t = t[:m.start()] + new + t[m.end():]
            # enable correctness check
            if fn == "ckks_tests_sym.c":
                t = sub1(t, "            // check_decode_decrypt_inpl(c0, c1_test_save, v, vlen, s_test_save, s_test_save_small,\n            //                           ntt_pte, index_map, &parms, temp_test_mem);\n",
                            "            check_decode_decrypt_inpl(c0, c1_test_save, v, vlen, s_test_save, s_test_save_small,\n                                      ntt_pte, index_map, &parms, temp_test_mem);\n            printf(\"VERIFY_OK test=%zu prime=%zu\\n\", testnum, i);\n", fn)
            else:
                t = sub1(t, "            // check_decode_decrypt_inpl(pk_c0, pk_c1, v, vlen, ntt_s_save, s_test_save_small, pterr,\n            //                           index_map, &parms, temp_test_mem);\n",
                            "            check_decode_decrypt_inpl(pk_c0, pk_c1, v, vlen, ntt_s_save, s_test_save_small, pterr,\n                                      index_map, &parms, temp_test_mem);\n            printf(\"VERIFY_OK test=%zu prime=%zu\\n\", testnum, i);\n", fn)
            return t
        rw(f"{dev}/test/{fn}", f)
    def g(t):
        t = sub1(t, "    // compare_poly(s1, pte_calc, s2, c0, n);", "    compare_poly(s1, pte_calc, s2, c0, n);", "ckks_tests_common.c")
        t = sub1(t, "        // se_assert(!err);", "        se_assert(!err);\n        if (err) { printf(\"DECODE_MISMATCH\\n\"); exit(3); }", "ckks_tests_common.c")
        return t
    rw(f"{dev}/test/ckks_tests_common.c", g)

def main():
    src, out, variant = sys.argv[1], sys.argv[2], sys.argv[3]
    configs = sys.argv[4].split(",") if len(sys.argv) > 4 else list(CONFIGS)
    modes = sys.argv[5].split(",") if len(sys.argv) > 5 else ["sym", "asym"]
    ns = [int(x) for x in sys.argv[6].split(",")] if len(sys.argv) > 6 else [1024, 2048, 4096]
    for c in configs:
        for mode in modes:
            for n in ns:
                name = f"{c}_{mode}_{n}"
                dst = os.path.join(out, variant, name)
                if os.path.exists(dst):
                    shutil.rmtree(dst)
                shutil.copytree(src, dst, symlinks=True)
                dev = dst
                apply_config(dev, CONFIGS[c])
                apply_main(dev, mode, n)
                if variant == "csv":
                    apply_csv(dev)
                elif variant == "verify":
                    apply_verify(dev, loop=False)
                elif variant == "verifyloop":
                    apply_verify(dev, loop=True)
                print(dst)

if __name__ == "__main__":
    main()
