# ARMv8-A signed lazy NTT v2 검증 및 성능 측정

## 구현과 보존 범위

새 구현은 device/lib/ntt_signed_v2.c에 추가했다. 기존 signed lazy NTT 함수 본문과 Montgomery/Barrett ASM은 변경하지 않았다. ntt.c에는 v2 선언·호출 두 줄을 추가하고 기존 활성 baseline 호출 한 줄을 주석 처리했다(원래 signed 호출은 이미 주석이었다). lib/CMakeLists.txt에 새 파일 한 줄만 추가했다. 근거: this-task.diff, before.diff, after.diff, new-v2-source.c. [확인됨]

ONE_SHOT 경로의 그룹별 mont(root,R2), u±v butterfly, 마지막 mont(x,R)와 부호 마스크 보정은 동일하다. 64 KiB scratch와 입력/출력 복사를 없애고 입력 ZZ 배열의 대응 signed 32-bit 타입을 사용한다. q/inv_q/R/R2는 시작에서 읽는다. 원본 OTF 경로는 twiddle-domain 처리가 다르므로 OTF 및 q>=0x20000000는 원본 함수로 위임한다. 이 결과는 제공된 27-bit 소수와 N 범위에 대한 검증이며 다른 모든 q/N의 안전성 증명은 아니다. [확인됨]

원본 `mul x7,x6,x4`의 하위 32비트가 `smaddl ...,w7,...`에 signed로 전달된다. v2의 `mul w[k],w[z],w[qinv]`도 같은 하위 32비트이며 smaddl의 부호 해석이 같다. 원본 LSR64 후 int32 결과와 v2 ASR64 후 int32 결과는 하위 32비트가 같다. inline asm은 non-volatile이고 memory/cc clobber가 없다. 입력·출력·early-clobber 제약으로 의존성을 표현한다. [확인됨]

## 검증 1–4

1. N 3개 × 소수 3개 × (무작위 1,000 + 경계 8) = 9,072개 벡터에서 원본/v2 비트 동일. 별도 signed Montgomery 산술 900,072건도 동일. 근거: kernel-validation.log, validate-v2.c. [확인됨]

2. 4개 구성 × SYM/ASYM main × N 3개 = 24개 Release build의 ckks_public 및 ntt_exact 모두 PASS. ckks_public은 각 빌드에서 기본 9개 메시지, 세 결정적 fixture, 두 mode의 exact modular 복호화와 decode를 검증하며 Release에서도 require가 살아 있다. 각 구성별 public ntt dispatch도 확인했다. [확인됨]

3. Mont.+Lazy의 6개 ASan/UBSan build에서 동일 검사 모두 PASS, sanitizer 오류 0건. 첫 sandbox 실행은 LeakSanitizer ptrace 제약으로 실패했고 matrix-driver.log 및 해당 correctness.log에 보존했다. 승인 후 샌드박스 밖에서 재실행했으며 sanitizer-native-*.log에 보존했다. 실패를 정상 성능 결과로 집계하지 않았다. [확인됨]

전체 decode 최대 오차 = 0.0584666722 < 0.1. tolerance 변경 없음. 근거: matrix.json 및 각 build/Testing/Temporary/LastTest.log. [확인됨]

4. main-L0 hot butterfly는 원본 15개, v2 13개 instruction이다(로드~loop back branch 포함). v2는 계수 load/store 각 2개이며 q/inv_q load가 hot loop에서 사라졌다. hot loop는 scalar이다. original은 64 KiB scratch가 남고 v2는 96-byte 저장 프레임이다. 근거: 아래 추출 및 전체 disassembly. [확인됨]

원본:

```asm
c0830:	b8617a82 	ldr	w2, [x20, x1, lsl #2]
   c0834:	b8617905 	ldr	w5, [x8, x1, lsl #2]
   c0838:	b9400263 	ldr	w3, [x19]
   c083c:	b9400e64 	ldr	w4, [x19, #12]
   c0840:	9b2a7ca6 	smull	x6, w5, w10
   c0844:	9b047cc7 	mul	x7, x6, x4
   c0848:	9b2318e5 	smaddl	x5, w7, w3, x6
   c084c:	d360fca5 	lsr	x5, x5, #32
   c0850:	0b050040 	add	w0, w2, w5
   c0854:	4b050042 	sub	w2, w2, w5
   c0858:	b8217a80 	str	w0, [x20, x1, lsl #2]
   c085c:	b8217902 	str	w2, [x8, x1, lsl #2]
   c0860:	91000421 	add	x1, x1, #0x1
   c0864:	eb09003f 	cmp	x1, x9
   c0868:	54fffe41 	b.ne	c0830 <v4_cr_signed_ntt_lazy_inpl+0x160>  // b.any
```

v2:

```asm
120124:	b9400001 	ldr	w1, [x0]
  120128:	b8757803 	ldr	w3, [x0, x21, lsl #2]
  12012c:	9b247c66 	smull	x6, w3, w4
  120130:	1b147cc7 	mul	w7, w6, w20
  120134:	9b3318e2 	smaddl	x2, w7, w19, x6
  120138:	9360fc42 	asr	x2, x2, #32
  12013c:	0b020023 	add	w3, w1, w2
  120140:	4b020021 	sub	w1, w1, w2
  120144:	b9000003 	str	w3, [x0]
  120148:	b8357801 	str	w1, [x0, x21, lsl #2]
  12014c:	91001000 	add	x0, x0, #0x4
  120150:	eb0000bf 	cmp	x5, x0
  120154:	54fffe81 	b.ne	120124 <cr_signed_ntt_lazy_inpl_v2+0x124>  // b.any
```

명령어 수 감소만으로 전체 speedup을 정량 설명하지 않는다. 복사 제거, register 배정, 함수 크기/배치 변경도 함께 발생했다. 각 변화의 독립 기여도는 이번 실험에서 분리하지 않았다. [확인됨]

## 전체 암호화 조건과 비교 한계

GCC 12.2.0, Release -O3 -mcpu=cortex-a76 -fno-strict-aliasing -fPIC 및 기존 defines 유지(라이브러리 -fPIC, CMake 테스트 실행 파일 -fPIE). 실제 compile_commands.json은 각 격리 build에 보존하고 검사했다. N1024/2048은 q0 한 개, N4096은 q0/q1/q2, scale=2^20, ONE_SHOT. taskset -c 2, 기존 perf_event_open user-only cycles backend, pinned, enabled/running 검사. Warm-up 5, 메시지·소수마다 1000, 기존 9패턴. 측정 중 소스/ELF/options 고정. [확인됨]

10/22의 격리 소스/키를 복사해서 같은 선택을 유지했다. secret key 파일은 동일하며 공개키/오류/PRNG의 과거 OS-random 바이트까지 같다고 주장하지 않는다. main의 SYM/ASYM 및 public ntt dispatch를 확인했다. 기존 M1/10월 결과나 ELF는 수정하지 않았다. [확인됨]

타이머는 기존 암호화 함수 바로 앞/뒤다. 함수 내부 root 생성은 포함하며 외부 encoding, key 생성/복원, PRNG 준비, 검증/출력은 제외한다. **개별 표본 보존을 위해 격리 test 소스에 end read 이후 raw_cycles[1000] 저장 및 각 prime loop 종료 후 RAW 출력이 추가됐다.** 이는 bracket 밖이지만 stack/cache/code placement와 다음 호출 상태에 영향을 줄 수 있다. 따라서 과거와 호출 경계·알고리즘·반복 조건은 같아도 실행 환경까지 완전히 동일하다고 표현할 수 없다. [확인됨]

현재 대표값은 1000개 원시값 평균 → 9패턴 동일 가중 평균 → N4096의 3 prime 산술평균이다. 10/22는 prime-message mean을 정수 절삭한 출력의 평균이었다. 새 표는 원시값의 소수 평균을 보존하며, 같은 절삭 방식의 참고값도 core-summary.csv legacy_integer_mean에 저장했다. 차이는 1 cycle 미만이며 비교는 이 정의 차이를 포함한다. [확인됨]

## 24조건 평균 CPU cycles와 동일 세션 Baseline 대비 speedup

| 구성 | Mode | N1024 cycles | Speedup | N2048 cycles | Speedup | N4096 cycles | Speedup |
|---|---|---:|---:|---:|---:|---:|---:|
| Baseline | SYM | 340,596.38 | 1.000× | 722,670.33 | 1.000× | 1,525,363.78 | 1.000× |
| Baseline | ASYM | 404,380.48 | 1.000× | 874,398.35 | 1.000× | 1,880,776.44 | 1.000× |
| Barrett Mul | SYM | 299,081.84 | 1.139× | 631,194.39 | 1.145× | 1,324,809.51 | 1.151× |
| Barrett Mul | ASYM | 345,947.07 | 1.169× | 744,507.41 | 1.174× | 1,594,921.40 | 1.179× |
| Mont.+Lazy NTT(v2) | SYM | 180,233.60 | 1.890× | 366,820.57 | 1.970× | 743,341.50 | 2.052× |
| Mont.+Lazy NTT(v2) | ASYM | 164,263.06 | 2.462× | 340,099.97 | 2.571× | 703,775.57 | 2.672× |
| Integrated(v2) | SYM | 178,669.54 | 1.906× | 363,287.15 | 1.989× | 734,495.19 | 2.077× |
| Integrated(v2) | ASYM | 178,409.60 | 2.267× | 364,754.04 | 2.397× | 751,136.61 | 2.504× |

모든 24조건 PASS. 원시 로그 *.stdout/*.stderr 및 core-*.csv, 소수별 core-prime-summary.csv, 입력별 SD/min/max core-input-summary.csv, 실행/환경 measurement-runs.json. [확인됨]

## 10/22 원본 구현과 비교

동일 세션 speedup과 달리 아래는 서로 다른 날짜·ELF·난수·raw 저장 방식의 비교다. 전부를 v2의 인과적 개선율로 해석하지 않는다. [확인됨]

| 구성 | Mode | N | 10/22 cycles | 이번 cycles | 변화 % |
|---|---|---:|---:|---:|---:|
| Baseline | SYM | 1024 | 340,770.67 | 340,596.38 | -0.05% |
| Baseline | SYM | 2048 | 722,357.11 | 722,670.33 | +0.04% |
| Baseline | SYM | 4096 | 1,524,565.85 | 1,525,363.78 | +0.05% |
| Baseline | ASYM | 1024 | 404,270.67 | 404,380.48 | +0.03% |
| Baseline | ASYM | 2048 | 874,069.33 | 874,398.35 | +0.04% |
| Baseline | ASYM | 4096 | 1,879,820.00 | 1,880,776.44 | +0.05% |
| Barrett Mul | SYM | 1024 | 299,191.11 | 299,081.84 | -0.04% |
| Barrett Mul | SYM | 2048 | 631,035.89 | 631,194.39 | +0.03% |
| Barrett Mul | SYM | 4096 | 1,324,287.70 | 1,324,809.51 | +0.04% |
| Barrett Mul | ASYM | 1024 | 345,958.11 | 345,947.07 | -0.00% |
| Barrett Mul | ASYM | 2048 | 744,527.00 | 744,507.41 | -0.00% |
| Barrett Mul | ASYM | 4096 | 1,594,726.81 | 1,594,921.40 | +0.01% |
| Mont.+Lazy NTT(v2) | SYM | 1024 | 198,756.11 | 180,233.60 | -9.32% |
| Mont.+Lazy NTT(v2) | SYM | 2048 | 406,093.00 | 366,820.57 | -9.67% |
| Mont.+Lazy NTT(v2) | SYM | 4096 | 825,803.81 | 743,341.50 | -9.99% |
| Mont.+Lazy NTT(v2) | ASYM | 1024 | 192,092.22 | 164,263.06 | -14.49% |
| Mont.+Lazy NTT(v2) | ASYM | 2048 | 400,286.78 | 340,099.97 | -15.04% |
| Mont.+Lazy NTT(v2) | ASYM | 4096 | 832,183.93 | 703,775.57 | -15.43% |
| Integrated(v2) | SYM | 1024 | 204,213.44 | 178,669.54 | -12.51% |
| Integrated(v2) | SYM | 2048 | 415,177.11 | 363,287.15 | -12.50% |
| Integrated(v2) | SYM | 4096 | 842,162.96 | 734,495.19 | -12.78% |
| Integrated(v2) | ASYM | 1024 | 203,613.78 | 178,409.60 | -12.38% |
| Integrated(v2) | ASYM | 2048 | 420,579.11 | 364,754.04 | -13.27% |
| Integrated(v2) | ASYM | 4096 | 871,449.70 | 751,136.61 | -13.81% |

근거: core-summary.csv 및 원본 execution-results.json. [확인됨]

## NTT 커널 비교

기존 bench는 수정하지 않고 새 measure-kernel.c에 V4v2를 variant=7로 추가했다. V0~V6의 기존 objects를 재사용했다. 주 옵션과 비벡터화 옵션 각각 4개 주소 배치, 5 warm-up, 1000회 × 9개 커널 입력 × 3 primes × N 3개. 8변형의 시작 순서를 회전시키고 AB/BA 방향을 교차했다. 기존 7변형과 새 8변형은 interleaving/cache 문맥이 같지는 않다. 커널 9패턴은 CKKS 인코딩 메시지 9개와 다르다. [확인됨]

root 사전 계산은 별도 prepare로 측정했다. V4v2는 원본 V4와 같은 root를 사용하고 그룹 내부 mont(root,R2)는 kernel에 포함된다. 기존 V5/V6의 최종 정규화도 포함한다. 반복별 raw는 kernel-*.stdout(CSV 형식)에 보존했다. 각 값은 배치별 27000 표본 중앙값의 네 배치 중앙값이다. 표준편차·최솟값·최댓값은 kernel-placement-summary.csv, prepare 결과는 kernel-summary.csv에 있다. [확인됨]

### 주 측정

| 구현 | N1024 cycles | Speedup | N2048 cycles | Speedup | N4096 cycles | Speedup |
|---|---:|---:|---:|---:|---:|---:|
| V0 | 110,518.00 | 1.000× | 242,693.00 | 1.000× | 529,170.00 | 1.000× |
| V1 | 90,712.00 | 1.218× | 198,780.00 | 1.221× | 432,622.00 | 1.223× |
| V2 | 62,604.50 | 1.765× | 136,187.50 | 1.782× | 294,876.50 | 1.795× |
| V3 | 63,054.50 | 1.753× | 138,162.00 | 1.757× | 300,639.00 | 1.760× |
| V4 | 39,771.50 | 2.779× | 84,778.00 | 2.863× | 180,216.00 | 2.936× |
| V5 | 38,582.50 | 2.864× | 86,233.00 | 2.814× | 194,122.25 | 2.726× |
| V6 | 29,000.50 | 3.811× | 62,940.00 | 3.856× | 141,274.50 | 3.746× |
| V4v2 | 30,534.00 | 3.620× | 64,807.00 | 3.745× | 137,472.00 | 3.849× |

표의 실제 계측·집계 결과 [확인됨]

### 보조 측정: -fno-tree-vectorize

| 구현 | N1024 cycles | Speedup | N2048 cycles | Speedup | N4096 cycles | Speedup |
|---|---:|---:|---:|---:|---:|---:|
| V0 | 110,543.00 | 1.000× | 242,766.00 | 1.000× | 529,208.00 | 1.000× |
| V1 | 90,760.00 | 1.218× | 198,865.50 | 1.221× | 432,724.50 | 1.223× |
| V2 | 62,680.50 | 1.764× | 136,254.00 | 1.782× | 294,908.00 | 1.794× |
| V3 | 63,068.50 | 1.753× | 138,192.50 | 1.757× | 300,633.00 | 1.760× |
| V4 | 41,219.00 | 2.682× | 87,552.00 | 2.773× | 185,674.75 | 2.850× |
| V5 | 38,395.00 | 2.879× | 85,451.50 | 2.841× | 193,026.50 | 2.742× |
| V6 | 42,976.50 | 2.572× | 95,176.00 | 2.551× | 215,168.00 | 2.460× |
| V4v2 | 30,564.00 | 3.617× | 64,846.00 | 3.744× | 137,579.00 | 3.847× |

표의 실제 계측·집계 결과 [확인됨]

## 환경과 제한

관측 온도 70.50–85.35°C, 주파수 kHz ['1600000', '1900000', '2400000'], governor ['ondemand']. 전역 설정 변경 없음. 개별 실행의 전체 표본·순서는 measurement-runs.json에 기록했다. CPU frequency 고정 또는 완전한 온도 안정화는 주장하지 않는다. [확인됨]

온도는 과거 10/22의 61.70–78.75°C 범위와 같지 않다. vcgencmd get_throttled는 /dev/vcio 부재로 조회 실패했다(environment-final.json). 따라서 throttling 부재나 완전히 같은 열 조건을 보증할 수 없다. 동일 세션 표와 날짜 간 비교를 분리하고 논문에서는 이 한계를 명시해야 한다. [확인됨]

Cortex-M4의 1.75×→3.91×는 사용자 제공 참고값이며 이번 환경에서 재검증하지 않았다. ARMv8-A 결과는 위 실측값만 사용해야 한다. [찾을 수 없음]

전체 측정은 PMU 오류 없이 완료했으며 empty-bracket 결과는 각 kernel stderr의 OVERHEAD에 원시 보존했다. overhead를 결과에서 차감하지 않았다. PMU 자체는 sandbox에서 동작했으며 sandbox 외 승인은 sanitizer에만 필요했다. [확인됨]

## 코드 diff

```diff
--- before/device/lib/ntt.c
+++ after/device/lib/ntt.c
@@ -443,6 +443,7 @@
     }
 }
 
+void cr_signed_ntt_lazy_inpl_v2(const Parms *, const ZZ *, ZZ *);
 void ntt_inpl(const Parms *parms, const ZZ *ntt_roots, ZZ *vec)
 {
     se_assert(parms && parms->curr_modulus && vec);
@@ -462,10 +463,11 @@
         if (vec[i] >= q) vec[i] -= q;
     }
 #else
-    ntt_non_lazy_inpl(parms, ntt_roots, vec);
+    // ntt_non_lazy_inpl(parms, ntt_roots, vec);
     // ntt_non_lazy_inpl_test(parms, ntt_roots, vec);
     // ntt_non_lazy_inpl_test_v1(parms, ntt_roots, vec);
     // cr_signed_ntt_lazy_inpl(parms, ntt_roots, vec);
+    cr_signed_ntt_lazy_inpl_v2(parms, ntt_roots, vec);
 #endif
 }
 
--- before/device/lib/CMakeLists.txt
+++ after/device/lib/CMakeLists.txt
@@ -17,6 +17,7 @@
 	${CMAKE_CURRENT_LIST_DIR}/timer.c
 	${CMAKE_CURRENT_LIST_DIR}/uint_arith.c
 	${CMAKE_CURRENT_LIST_DIR}/ntt.c
+	${CMAKE_CURRENT_LIST_DIR}/ntt_signed_v2.c
 	${CMAKE_CURRENT_LIST_DIR}/intt.c
 	${CMAKE_CURRENT_LIST_DIR}/seal_embedded.c
 )
--- /dev/null
+++ device/lib/ntt_signed_v2.c
@@ -0,0 +1,55 @@
+/* ARMv8-A signed lazy NTT: original butterfly and Montgomery convention. */
+#include "ntt.h"
+#include <stdint.h>
+
+void cr_signed_ntt_lazy_inpl(const Parms *, const ZZ *, ZZ *);
+
+static inline int32_t mont_mul_v2(int32_t a, int32_t b, int32_t q, uint32_t qinv)
+{
+    int64_t z, r;
+    int32_t k;
+    __asm (
+        "smull %x[z], %w[a], %w[b]\n\t"
+        "mul %w[k], %w[z], %w[qinv]\n\t"
+        "smaddl %x[r], %w[k], %w[q], %x[z]\n\t"
+        : [z] "=&r" (z), [k] "=&r" (k), [r] "=&r" (r)
+        : [a] "r" (a), [b] "r" (b), [q] "r" (q), [qinv] "r" (qinv));
+    return (int32_t)(r >> 32);
+}
+
+void cr_signed_ntt_lazy_inpl_v2(const Parms *parms, const ZZ *roots, ZZ *vec)
+{
+    se_assert(parms && parms->curr_modulus && vec);
+#ifdef SE_NTT_OTF
+    /* The original OTF branch has different twiddle-domain handling. */
+    cr_signed_ntt_lazy_inpl(parms, roots, vec);
+#else
+    const Modulus *mod = parms->curr_modulus;
+    if (mod->value >= 0x20000000) {
+        cr_signed_ntt_lazy_inpl(parms, roots, vec);
+        return;
+    }
+    const size_t n = parms->coeff_count, logn = parms->logn;
+    const int32_t q = (int32_t)mod->value;
+    const uint32_t qinv = mod->inv_q;
+    const int32_t r2 = (int32_t)mod->R2, r = (int32_t)mod->R;
+    int32_t *sv = (int32_t *)vec;
+    se_assert(roots && n >= 1024 && n <= 16384 && !(n & (n - 1)));
+    se_assert(logn == get_log2(n));
+    se_assert((uint64_t)(logn + 1) * (uint32_t)q < INT32_MAX);
+    for (size_t i = 0, h = 1, tt = n / 2; i < logn; ++i, h *= 2, tt /= 2) {
+        for (size_t j = 0, start = 0; j < h; ++j, start += 2 * tt) {
+            int32_t s = mont_mul_v2((int32_t)roots[h + j], r2, q, qinv);
+            for (size_t k = start; k < start + tt; ++k) {
+                int32_t u = sv[k], v = mont_mul_v2(sv[k + tt], s, q, qinv);
+                sv[k] = u + v;
+                sv[k + tt] = u - v;
+            }
+        }
+    }
+    for (size_t i = 0; i < n; ++i) {
+        int32_t x = mont_mul_v2(sv[i], r, q, qinv);
+        sv[i] = x + ((x >> 31) & q);
+    }
+#endif
+}
```

기존 변경과 이번 변경의 구분은 before.diff/after.diff와 this-task.diff를 참조한다. 원본 24 ELF 무변경 및 추적 파일 허용 변경 검사는 preservation.json에 기록한다. commit/push/branch 변경 없음. [확인됨]

## 최종 확인 및 해석

main; N=1024: 원본 V4/v2=1.303×, N=2048: 원본 V4/v2=1.308×, N=4096: 원본 V4/v2=1.311×. 같은 세션·집계 방식의 비교다. [확인됨]
novec; N=1024: 원본 V4/v2=1.349×, N=2048: 원본 V4/v2=1.350×, N=4096: 원본 V4/v2=1.350×. 같은 세션·집계 방식의 비교다. [확인됨]

주 측정의 대표 중앙값에서 N1024/2048은 V6가 가장 빠르고, N4096은 V4v2가 가장 빠르다. 보조 측정은 모든 N에서 V4v2가 가장 빠르다. 이는 집계 순위이며 모든 개별 입력에서의 순위를 뜻하지 않는다. [확인됨]

SYM은 세 N 모두 Integrated(v2)가 Mont.+Lazy(v2)보다 빠르지만 ASYM은 반대다. Barrett 추가 효과를 모든 mode에 일반화할 수 없으며, 그 원인의 추가 분리 실험은 이번 범위에 포함하지 않았다. [확인됨]

32개 측정 실행 모두 PASS. 전체 암호화 360,000개, 커널 5,184,000개, 사전 계산 576,000개 개별 cycles를 보존했다. kernel-*.csv는 동일 내용의 *.stdout에 대한 hard link다. 커널 raw 전체 5,760,000행의 N/q/input/repeat/variant 순서를 예상값과 대조하여 누락·중복·비양수 0개를 확인했다(raw-validation.json). [확인됨]

기존 Release ELF 24개, 10/22 결과 67개 파일, 11/4 커널 결과 94개 파일과 복사 진단 결과 59개 파일의 SHA256 불일치 0개. 신규 계측 ELF 32개도 전후 동일했다. 이번에 바뀐 기존 추적 파일은 lib/ntt.c와 lib/CMakeLists.txt뿐이며 원본 signed 함수 본문과 ASM은 동일하다. 새 ntt_signed_v2.c와 격리 진단 파일은 별도 추가했다. 근거: preservation.json. [확인됨]
