# SEAL-Embedded CKKS: signed lazy NTT 개선판(v2) ARMv8-A 구현 및 Apple M1 재측정 보고서

- 작성일: 2026-11-05
- 결과 폴더: `results/m1-v2-20261105-183139/`
- 비교 기준: `results/m1-fix-20261104-170935/` (원본 `cr_signed_ntt_lazy_inpl`에 `max_n → n` 수정만 한 상태)
- 표기 규칙
  - **[확인됨]**: 이번 작업에서 직접 실행하거나 파일로 확인한 사실
  - **[추정]**: 근거는 있지만 직접 확인하지 못한 해석
  - **[찾을 수 없음]**: 찾아봤지만 존재를 확인하지 못한 항목

---

## 0. 요약

| 항목 | 결과 |
|---|---|
| 구현 | `device/lib/ntt_signed_v2.c`에 `cr_signed_ntt_lazy_inpl_v2()`를 새로 추가했습니다. 원본 함수와 원본 asm은 수정하지 않았습니다. **[확인됨]** |
| 검증 1: 비트 동일성 | 9,081건 모두 원본 출력과 비트 단위로 같습니다(N 3개 × prime 3개 × (무작위 1,000 + 경계값 9)). Montgomery 곱셈 단독 비교 3,000,675건도 불일치 0건입니다. **[확인됨]** |
| 검증 2: 테스트 벡터 9개 | 4개 구성 × SYM/ASYM × N 3개 = 24개 조건이 모두 통과했습니다. **[확인됨]** |
| 검증 3: ASan | Mont.+Lazy 6개 조건에서 오류 0건입니다. **[확인됨]** |
| 검증 4: 디스어셈블 | butterfly 1쌍당 명령 수가 15개에서 13개로 줄었습니다. 반복마다 하던 `ldr q`, `ldr inv_q`가 사라졌고, 입력·출력 복사 루프도 없어졌습니다. **[확인됨]** |
| 성능 | 전체 암호화 사이클 기준으로 원본 구현(m1-fix) 대비 Mont.+Lazy NTT는 −2.0%~−4.8%, Integrated는 −0.8%~−5.3% 줄었습니다. Integrated ASYM speedup은 1.55× → 1.63~1.64×(N=2048/4096)가 됐습니다. **[확인됨]** |
| 영향 없는 구성 | Baseline과 Barrett Mul은 m1-fix 대비 −1.8%~+0.5%이고, 중앙값 기준으로는 ±0.1% 이내입니다. 두 세션의 측정 조건이 같다고 볼 수 있습니다. **[확인됨]** |

---

## 1. 시작 상태 확인 [확인됨]

- `git rev-parse HEAD`: `7aa1bf2d35ed3522101bf8dcbc07bac415e135fb`
  - 저장 파일: `git_head_before.txt`
- 브랜치: `montgomery_armv8`
- `git status`: ` M device/lib/ntt.c`, `?? results/`
  - 저장 파일: `git_status_before.txt`
- `git diff`(저장 파일: `diff_before.patch`)는 지난번 `m1-fix-20261104-170935/diff_after_fix.patch`와 **바이트 단위로 같습니다**(`cmp` 결과 일치).
  - 따라서 `device/lib/ntt.c:356`의 `for (size_t i = 0; i < n; i++)` 수정이 그대로 남아 있는 상태에서 시작했습니다.

---

## 2. 원본과 개선 사양 대조

### 2.1 `cr_signed_ntt_lazy_inpl` 원본 구조 (`device/lib/ntt.c:335–436`) [확인됨]

요청서의 "원본과 동일하게 유지할 부분"과 원본 코드를 한 줄씩 대조했습니다. **모두 일치**하므로 사양을 그대로 따랐습니다.

| 요청 사양 | 원본 코드 | 일치 |
|---|---|---|
| 그룹마다 `s = mont(root, R2)` | `ZZsign s = ntt_roots[h + j]; s = mul_mod_mont(s, (mod->R2), mod);` (373~374행) | 일치 |
| 보정 없는 butterfly | `v = mul_mod_mont(signed_vec[k + tt], s, mod); signed_vec[k] = (u + v); signed_vec[k + tt] = (u - v);` (378~381행) | 일치 |
| 마지막에 한 번만 정규화 | `signed_vec[i] = mul_mod_mont(signed_vec[i], (mod->R), mod); signed_vec[i] += (signed_vec[i]>>31)&(mod->value);` (429~431행) | 일치 |
| 30-bit prime 처리 | `q >= 0x20000000`이면 unsigned `mul_mod`/`add_mod`/`sub_mod` 경로를 탐(386~419행) | v2는 이 경우 원본 함수를 그대로 호출 |

추가로 다르게 처리한 점:
- 원본에는 `SE_NTT_OTF` 분기(twiddle을 즉석 계산)가 있습니다.
- v2는 `SE_NTT_OTF`일 때도 원본 함수를 그대로 호출하도록 했습니다. 측정 구성은 NTT=one-shot이라 이 경로를 타지 않습니다. **[확인됨]**

### 2.2 Montgomery 곱셈 asm 대조 (참고 구현 vs 원본 `cr_mont_mul_asm`, `uintmodarith.h:221–244`) [확인됨]

| 단계 | 원본 | v2 (참고 구현) | 결과 |
|---|---|---|---|
| z | `smull x6, %w[a], %w[b]` | `smull %x[z], %w[a], %w[b]` | 같음 |
| k | `mul x7, x6, x4` (64비트 곱, x4는 `ldr w4`로 0-확장한 inv_q) | `mul %w[k], %w[z], %w[qinv]` (32비트 곱) | 다음 단계에서 원본도 `w7`, 즉 하위 32비트만 쓰고, 64비트 곱의 하위 32비트 = `z_lo32 × inv_q mod 2^32`입니다. **비트폭·값 모두 같음** |
| k의 부호 | `smaddl`이 `w7`을 부호 확장 | `smaddl`이 `%w[k]`를 부호 확장 | 같음 |
| q | `ldr w3, [st_q]` 후 `smaddl`에서 부호 확장 | 레지스터 인자 `%w[q]`, 부호 확장 | q < 2^31이라 같음 |
| r | `smaddl %[res], w7, w3, x6` | `smaddl %x[r], %w[k], %w[q], %x[z]` | 같음 |
| 결과 | `lsr #32` 후 `int32`로 자름 | `(int32_t)(r >> 32)` (산술 시프트 후 자름) | 하위 32비트 = r[63:32]이라 **같음** |

- 따라서 참고 구현을 그대로 썼습니다.
- 3,000,675건의 단독 비교에서 불일치가 0건이었습니다(4.1절).

### 2.3 Apple clang 컴파일 [확인됨]

- 환경: Apple clang 17.0.0 (clang-1700.4.4.1), `-O3 -arch arm64 -Wall`
- 참고 asm은 **수정 없이 컴파일됩니다.**
  - `%x[...]`/`%w[...]` 수식자를 명시했기 때문에, `ntt_signed_v2.c`에서는 경고가 하나도 나오지 않습니다.
  - 나오는 경고 4건은 모두 기존 `uintmodarith.h:178`, `:239`의 `-Wasm-operand-widths`입니다.
- 비교: 원본 asm은 `volatile`과 `"memory"` 클로버 때문에 매 곱셈마다 `ldr w3`/`ldr w4`가 반복됩니다(5절). v2는 `volatile`과 `"memory"` 클로버를 뺐습니다.

---

## 3. 코드 diff [확인됨]

저장소 변경 사항은 아래가 전부입니다.
- 커밋, 브랜치 변경, stash는 하지 않았습니다(stash 0개, HEAD 그대로).
- 관련 파일
  - `diff_after.patch`: `git diff` 결과
  - `new_file_ntt_signed_v2.patch`: 새 파일(git 미추적)
  - `git_status_after.txt`

### 3.1 `git diff` (기존 추적 파일)

```diff
diff --git a/device/lib/CMakeLists.txt b/device/lib/CMakeLists.txt
@@ -16,6 +16,7 @@ set(SE_LIB_SOURCE_FILES ${SE_LIB_SOURCE_FILES}
 	${CMAKE_CURRENT_LIST_DIR}/ntt.c
+	${CMAKE_CURRENT_LIST_DIR}/ntt_signed_v2.c
 	${CMAKE_CURRENT_LIST_DIR}/intt.c
diff --git a/device/lib/ntt.c b/device/lib/ntt.c
@@ -20,6 +20,7 @@
 #if defined(SE_NTT_OTF) || defined(SE_NTT_ONE_SHOT)
 ZZ get_ntt_root(size_t n, ZZ q);  // defined below
 #endif
+void cr_signed_ntt_lazy_inpl_v2(const Parms *parms, const ZZ *ntt_roots, ZZ *vec);  // ntt_signed_v2.c
@@ -353,7 +354,7 @@ void cr_signed_ntt_lazy_inpl(...)   ← 지난번(m1-fix) 수정, 그대로 유지
-        for (size_t i = 0; i < max_n; i++)
+        for (size_t i = 0; i < n; i++)
@@ -457,6 +458,7 @@ void ntt_inpl(const Parms *parms, const ZZ *ntt_roots, ZZ *vec)
     // cr_signed_ntt_lazy_inpl(parms, ntt_roots, vec);
+    // cr_signed_ntt_lazy_inpl_v2(parms, ntt_roots, vec);
 #endif
```

`ntt.c`의 v2 전환 줄을 주석 상태로 둔 이유:
- HEAD부터 저장소의 `ntt_inpl()`은 Baseline(`ntt_non_lazy_inpl`)이 선택돼 있고, 원본 lazy 호출 줄은 주석입니다.
- v2 전환 줄을 같은 형태로 원본 줄 바로 아래에 두었습니다.
- Mont.+Lazy NTT와 Integrated 구성에서는 저장소 밖 사본에서 **v2 줄만 주석을 해제**합니다. 원본 호출 줄은 주석으로 남아 있습니다.
  - 생성 스크립트: `scripts/gen_trees.py`
  - 사본 diff 예시: `ntt.c` 457행 `// ntt_non_lazy_inpl(...)`, 461행 `cr_signed_ntt_lazy_inpl_v2(...)`
- 저장소 파일 자체에 "원본 대신 v2를 부르는" 활성 줄이 필요하시면, 이 주석 한 줄을 해제하고 457행을 주석 처리하면 됩니다. 다만 그렇게 하면 저장소 기본 구성이 Baseline에서 Mont.+Lazy로 바뀝니다.

### 3.2 새 파일 `device/lib/ntt_signed_v2.c` (핵심부)

```c
static inline int32_t mont_mul_v2(int32_t a, int32_t b, int32_t q, uint32_t qinv)
{
    int64_t z, r; int32_t k;
    __asm("smull  %x[z], %w[a], %w[b]\n\t"         // z = a*b
          "mul    %w[k], %w[z], %w[qinv]\n\t"      // k = z_lo * q'
          "smaddl %x[r], %w[k], %w[q], %x[z]\n\t"  // r = z + k*q
          : [z] "=&r"(z), [k] "=&r"(k), [r] "=&r"(r)
          : [a] "r"(a), [b] "r"(b), [q] "r"(q), [qinv] "r"(qinv));
    return (int32_t)(r >> 32);
}

void cr_signed_ntt_lazy_inpl_v2(const Parms *parms, const ZZ *ntt_roots, ZZ *vec)
{
    ...
    if (mod->value >= 0x20000000) { cr_signed_ntt_lazy_inpl(parms, ntt_roots, vec); return; }
    const int32_t q = (int32_t)mod->value;  const uint32_t qinv = (uint32_t)mod->inv_q;
    const int32_t R = (int32_t)mod->R;       const int32_t R2 = (int32_t)mod->R2;
    int32_t *x = (int32_t *)vec;            // 제자리 계산 (signed_vec, 복사 루프 제거)
    for (i < logn; h *= 2, tt /= 2)
        for (j < h; kstart += 2*tt) {
            const int32_t s = mont_mul_v2((int32_t)ntt_roots[h + j], R2, q, qinv);
            for (k = kstart; k < kstart + tt; k++) {
                int32_t u = x[k];
                int32_t v = mont_mul_v2(x[k + tt], s, q, qinv);
                x[k] = u + v;  x[k + tt] = u - v;     // lazy: 보정 없음
            }
        }
    for (i < n) { int32_t y = mont_mul_v2(x[i], R, q, qinv); y += (y >> 31) & q; x[i] = y; }
}
```

- 이 함수는 AArch64에서 `ZZ`가 32비트일 때만 컴파일됩니다. 그 외 환경에서는 v2가 원본을 그대로 호출합니다.
- `ZZ*`를 `int32_t*`로 접근해도 됩니다. C 표준상 같은 타입의 signed/unsigned 변형끼리는 aliasing이 허용됩니다.

---

## 4. 검증 결과

### 4.1 검증 1: 원본 대비 비트 동일성 [확인됨]

**방법**
- 하네스: `scripts/equiv_test.c`
- 측정에 쓰는 Release `-O3` `libseal_embedded.a`에 링크해, 원본 `cr_signed_ntt_lazy_inpl`과 `cr_signed_ntt_lazy_inpl_v2`에 같은 입력과 같은 twiddle 표를 넣고 출력을 `memcmp`로 비교했습니다.
- 출력이 [0, q) 범위에 있는지도 함께 확인했습니다.
- 시드는 `0x5EA1E2026`(splitmix64)입니다.
- 경계값 입력 9종: 전부 0, 전부 q−1, 전부 1, 0/q−1 교대, q−1/0 교대, δ₀, 마지막 계수만 q−1, 0..n−1 증가열, (q−1)/2 근처

**결과** (`verify1_equivalence.csv`)

| N | q | ψ | ψ 출처 | ψ^N ≡ −1 | 무작위 | 경계 | 불일치 |
|---:|---:|---:|---|:---:|---:|---:|---:|
| 1024 | 134012929 | 142143 | `get_ntt_root` | ✔ | 1,000 | 9 | 0 |
| 1024 | 134111233 | 27195555 | ψ₄₀₉₆^4 | ✔ | 1,000 | 9 | 0 |
| 1024 | 134176769 | 73264892 | ψ₄₀₉₆^4 | ✔ | 1,000 | 9 | 0 |
| 2048 | 134012929 | 85250 | `get_ntt_root` | ✔ | 1,000 | 9 | 0 |
| 2048 | 134111233 | 14868736 | ψ₄₀₉₆^2 | ✔ | 1,000 | 9 | 0 |
| 2048 | 134176769 | 46467125 | ψ₄₀₉₆^2 | ✔ | 1,000 | 9 | 0 |
| 4096 | 134012929 | 7470 | `get_ntt_root` | ✔ | 1,000 | 9 | 0 |
| 4096 | 134111233 | 3856 | `get_ntt_root` | ✔ | 1,000 | 9 | 0 |
| 4096 | 134176769 | 24149 | `get_ntt_root` | ✔ | 1,000 | 9 | 0 |
| **합계** | | | | | **9,000** | **81** | **0** |

ψ 출처에 관한 참고:
- 라이브러리 `get_ntt_root()`에는 N=1024/2048용 root가 134012929 하나만 있습니다. 다른 prime은 `se_assert(q == 134012929)`로 막혀 있습니다. **[확인됨]**
- 그래서 나머지 4개 조합은 N=4096용 ψ를 4096/N 제곱한 원시 2N차 단위근을 썼습니다(ψ^N ≡ −1 확인).
- 원본과 v2에 같은 표를 넣었으므로 구현 동일성 비교에는 문제가 없습니다.
- 다만 이 4개 조합은 SEAL-Embedded가 실제로 쓰는 파라미터는 아닙니다.

Montgomery 곱셈 단독 비교:
- 원본 `mul_mod_mont`(= `cr_mont_mul_asm`)와 `mont_mul_v2`를 비교했습니다.
- 대상: prime 3개 × (경계값 15×15 쌍 + 무작위 100만 쌍. a는 int32 전 범위, b는 (−q, q))
- 결과: 3,000,675건, 불일치 0건

### 4.2 검증 2: 기본 테스트 벡터 9개 (24개 조건) [확인됨]

- 지난번과 같은 검증용 빌드를 썼습니다.
  - prime마다 1회 암호화
  - `check_decode_decrypt_inpl` 호출
  - `compare_poly(pte_calc, decrypted)` 정확 일치 검사
  - `se_assert(!err)`로 decode 오차 < 0.1 검사
  - 빌드 옵션은 `-O3`에 assert만 켬
- 결과 파일: `verify2_summary.csv`, 로그: `verify_logs/`

| 구성 | SYM 1024 | SYM 2048 | SYM 4096 | ASYM 1024 | ASYM 2048 | ASYM 4096 |
|---|---|---|---|---|---|---|
| Baseline | 9/9 | 9/9 | 27/27 | 9/9 | 9/9 | 27/27 |
| Barrett Mul | 9/9 | 9/9 | 27/27 | 9/9 | 9/9 | 27/27 |
| Mont.+Lazy NTT (v2) | 9/9 | 9/9 | 27/27 | 9/9 | 9/9 | 27/27 |
| Integrated (v2) | 9/9 | 9/9 | 27/27 | 9/9 | 9/9 | 27/27 |

- 24개 조건 모두 종료 코드 0, 불일치 0건, "All tests passed."가 출력됐습니다.
- N=4096은 벡터 9개 × prime 3개 = 27회입니다.
- 검증 바이너리에 v2 심볼(`_cr_signed_ntt_lazy_inpl_v2`)이 링크된 것도 `nm`으로 확인했습니다.

### 4.3 검증 3: AddressSanitizer (Mont.+Lazy, 6개 조건) [확인됨]

- 빌드 옵션: `-O1 -g -fsanitize=address -fno-omit-frame-pointer`, assert 켬. `ntt_signed_v2.c`도 계측 대상에 포함됩니다.
- 결과 파일: `verify3_asan.csv`, 로그: `asan/`

| 조건 | ASan 오류 | 통과 |
|---|---:|---|
| SYM 1024 / 2048 / 4096 | 0 / 0 / 0 | 9 / 9 / 27 |
| ASYM 1024 / 2048 / 4096 | 0 / 0 / 0 | 9 / 9 / 27 |

### 4.4 검증 4: butterfly hot loop 명령어 비교 [확인됨]

- 대상 바이너리: `trees_v2/meas/montlazy_sym_1024`. Release `-O3`이고, 측정 바이너리와 같은 빌드입니다.
- 도구: `otool -tV`
- 저장 파일: `disasm/cr_signed_ntt_lazy_inpl.s`(316줄), `disasm/cr_signed_ntt_lazy_inpl_v2.s`(78줄), `disasm/ntt_inpl.s`, 그리고 비교표 `disasm/hot_loop_side_by_side.txt`
- `ntt_inpl`은 `b _cr_signed_ntt_lazy_inpl_v2`(꼬리 호출) 한 줄로, v2가 실제로 호출됩니다.

```
ORIGINAL (27-bit 경로, 15 insns/pair)  | V2 (13 insns/pair)
ldr    w2, [x17]                       | ldr    w21, [x19]
ldr    w5, [x17, x25, lsl #2]          | ldr    w22, [x19, x16, lsl #2]
ldr    w3, [x23]          ← q 재로드   | smull  x23, w22, w7
ldr    w4, [x23, #0xc]    ← q' 재로드  | mul    w24, w23, w10
smull  x6, w5, w16                     | smaddl x25, w24, w8, x23
mul    x7, x6, x4                      | lsr    x22, x25, #32
smaddl x5, w7, w3, x6                  | add    w23, w21, w22
lsr    x5, x5, #32                     | str    w23, [x19]
add    w0, w5, w2                      | sub    w21, w21, w22
str    w0, [x17]                       | str    w21, [x19, x16, lsl #2]
sub    w0, w2, w5                      | add    x19, x19, #0x4
str    w0, [x17, x25, lsl #2]          | subs   x20, x20, #0x1
add    x17, x17, #0x4                  | b.ne   loop
subs   x1, x1, #0x1                    |
b.ne   loop                            |
```

- 원본은 asm의 `volatile` + `"memory"` 클로버 때문에, butterfly마다 `q`와 `inv_q`를 메모리에서 다시 읽습니다(`ldr` 2개). v2는 둘 다 레지스터(`w8`, `w10`)에 고정돼 있습니다.
- 원본은 `signed_vec` 스택 버퍼로 복사해 들어가는 루프와, 정규화 후 `vec`로 복사해 나오는 루프가 있습니다. v2는 정규화 루프 하나만 남았습니다.
- 원본 함수의 136행 이후 NEON 코드(`umull.2d` 등)는 30-bit prime 분기이고, 측정 경로(27-bit)와 무관합니다.

---

## 5. 측정 결과

### 5.1 측정 방법과 환경 [확인됨]

**측정 방법**: 지난번(m1-fix)과 같습니다.
- 컴파일: Apple clang 17.0.0, `-O3 -DNDEBUG -std=gnu11 -arch arm64 -fPIC`, `-DSE_UNDEF_ASSERT_FORCE`
- 계측: `m1cycles.c`(kperf, `CPMU_CORE_CYCLE`, `USER_INTERACTIVE` QoS), `sudo`로 실행
- 반복: (벡터, prime)마다 warm-up 5회 + 1,000회
- 입력: 테스트 벡터 9개
- 대표값: prime별 1,000회 평균을 구하고, N=4096이면 prime 3개를 평균한 뒤, 9개 벡터를 평균합니다.
- 실행 스크립트: `run_measurements.sh`, 원시 로그: `raw/meas/`
- 반복별 사이클: `raw/csv/` → `iter_cycles.csv`(360,000개)
  - 측정 구간 밖에서 배열에 저장하는 줄 하나만 추가한 별도 바이너리로 수집했습니다.
  - 원 하네스 평균과의 차이는 0.0~1.46%입니다.

**실행 환경** (`raw/env.txt`)

| 항목 | 이번 (v2) | 지난번 (m1-fix) |
|---|---|---|
| 일시 | 2026-11-05 18:38~18:40 | 2026-11-04 17:19 |
| macOS / CPU | 15.6.1 (24G90) / Apple M1 | 동일 |
| 전원 | **배터리**(98%, 방전 중) | **AC 전원**(90%, 충전 중) |
| load average (1/5/15분) | 시작 1.50 / 3.44 / 4.80 → 종료 1.45 / 2.95 / 4.48 | 시작 1.44 / 2.17 / 2.63 |

전원 조건이 달랐던 점에 대해 [추정]:
- 두 세션의 전원 조건은 다릅니다.
- 하지만 v2와 무관한 Baseline과 Barrett Mul의 **중앙값**이 지난번과 ±0.1% 이내(평균은 −1.8%~+0.5%)로 일치합니다(5.3절).
- 따라서 이 차이가 사이클 수에 준 영향은 무시할 수 있는 수준으로 보입니다.
- 사이클 단위 측정이므로 클럭 주파수 차이 자체는 상쇄됩니다.

### 5.2 24개 조건 평균 사이클과 Baseline 대비 speedup [확인됨]

괄호 안은 같은 세션 Baseline 대비 speedup(C_baseline / C_opt)입니다.

| Configuration | Mode | N = 1024 | N = 2048 | N = 4096 |
|---|---|---:|---:|---:|
| Baseline | SYM | 116,327 | 237,101 | 477,625 |
| | ASYM | 108,260 | 229,167 | 478,081 |
| Barrett Mul | SYM | 112,886 (1.03×) | 234,531 (1.01×) | 480,014 (1.00×) |
| | ASYM | 109,427 (0.99×) | 232,203 (0.99×) | 495,019 (0.97×) |
| Mont.+Lazy NTT (v2) | SYM | 94,729 (1.23×) | 189,028 (1.25×) | 377,186 (1.27×) |
| | ASYM | 77,936 (1.39×) | 159,783 (1.43×) | 327,113 (1.46×) |
| Integrated (v2) | SYM | 86,754 (1.34×) | 172,357 (1.38×) | 343,240 (1.39×) |
| | ASYM | 68,403 (1.58×) | 140,530 (1.63×) | 291,572 (1.64×) |

- 반복별 분포 통계(평균, 중앙값, 표준편차, min, p5, p95, max)는 `analysis_tables.md`의 Table D와 `summary.csv`에 있습니다.
- 벡터별·prime별 평균은 `meas_per_test_prime.csv`에 있습니다.
- 참고: `analysis_tables.md`의 Table B(논문 대비 비교)와 Table C(Test 0만)는 지난번 분석 스크립트(`scripts/analyze_m1fix_original.py`)를 그대로 돌린 결과입니다.

### 5.3 지난번(m1-fix, 원본 구현)과 비교 [확인됨]

- 사이클 변화 = (v2 세션 − m1-fix) / m1-fix입니다.
- 중앙값 변화는 반복별 CSV(조건당 9,000개 또는 27,000개)의 중앙값으로 계산했습니다.
- 결과 파일: `compare_m1fix_vs_v2.csv`

| Configuration | Mode | N | m1-fix 평균 | m1-fix speedup | v2 세션 평균 | v2 세션 speedup | 사이클 변화 | 중앙값 변화 |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Baseline | SYM | 1024 | 116,101 | 1.00× | 116,327 | 1.00× | +0.2% | −0.0% |
| Baseline | SYM | 2048 | 236,593 | 1.00× | 237,101 | 1.00× | +0.2% | +0.0% |
| Baseline | SYM | 4096 | 477,749 | 1.00× | 477,625 | 1.00× | −0.0% | +0.0% |
| Baseline | ASYM | 1024 | 110,290 | 1.00× | 108,260 | 1.00× | −1.8% | −0.0% |
| Baseline | ASYM | 2048 | 229,320 | 1.00× | 229,167 | 1.00× | −0.1% | −0.0% |
| Baseline | ASYM | 4096 | 477,813 | 1.00× | 478,081 | 1.00× | +0.1% | +0.0% |
| Barrett Mul | SYM | 1024 | 113,986 | 1.02× | 112,886 | 1.03× | −1.0% | −0.0% |
| Barrett Mul | SYM | 2048 | 233,270 | 1.01× | 234,531 | 1.01× | +0.5% | +0.0% |
| Barrett Mul | SYM | 4096 | 479,058 | 1.00× | 480,014 | 0.99× | +0.2% | +0.0% |
| Barrett Mul | ASYM | 1024 | 109,915 | 1.00× | 109,427 | 0.99× | −0.4% | +0.1% |
| Barrett Mul | ASYM | 2048 | 232,106 | 0.99× | 232,203 | 0.99× | +0.0% | −0.1% |
| Barrett Mul | ASYM | 4096 | 495,540 | 0.96× | 495,019 | 0.97× | −0.1% | −0.0% |
| Mont.+Lazy NTT | SYM | 1024 | 96,620 | 1.20× | 94,729 | 1.23× | **−2.0%** | −2.3% |
| Mont.+Lazy NTT | SYM | 2048 | 193,797 | 1.22× | 189,028 | 1.25× | **−2.5%** | −2.4% |
| Mont.+Lazy NTT | SYM | 4096 | 386,600 | 1.24× | 377,186 | 1.27× | **−2.4%** | −2.5% |
| Mont.+Lazy NTT | ASYM | 1024 | 81,867 | 1.35× | 77,936 | 1.39× | **−4.8%** | −4.3% |
| Mont.+Lazy NTT | ASYM | 2048 | 165,691 | 1.38× | 159,783 | 1.43× | **−3.6%** | −4.2% |
| Mont.+Lazy NTT | ASYM | 4096 | 341,461 | 1.40× | 327,113 | 1.46× | **−4.2%** | −4.3% |
| Integrated | SYM | 1024 | 87,462 | 1.33× | 86,754 | 1.34× | **−0.8%** | −2.6% |
| Integrated | SYM | 2048 | 175,598 | 1.35× | 172,357 | 1.38× | **−1.8%** | −2.7% |
| Integrated | SYM | 4096 | 349,640 | 1.37× | 343,240 | 1.39× | **−1.8%** | −2.8% |
| Integrated | ASYM | 1024 | 71,755 | 1.54× | 68,403 | 1.58× | **−4.7%** | −4.8% |
| Integrated | ASYM | 2048 | 147,711 | 1.55× | 140,530 | 1.63× | **−4.9%** | −4.9% |
| Integrated | ASYM | 4096 | 307,745 | 1.55× | 291,572 | 1.64× | **−5.3%** | −5.5% |

### 5.4 해석

**[확인됨]**
- v2는 lazy NTT를 쓰는 12개 조건 모두에서 사이클을 줄였습니다.
- 중앙값 기준으로 SYM은 −2.3%~−2.8%, ASYM은 −4.2%~−5.5%입니다.
- 평균 기준 Integrated SYM N=1024(−0.8%)는 중앙값(−2.6%)보다 작게 나왔는데, 이상치(반복별 max가 중앙값의 약 2배)의 영향입니다.

**[추정]**
- **ASYM의 감소폭이 SYM의 약 2배인 이유**: prime마다 호출하는 NTT 횟수가 ASYM은 3회(`ckks_asym.c:242, 265, 282`), SYM은 2회(`ckks_sym.c:266, 306`)라서, 암호화 1회에서 NTT가 차지하는 비중이 ASYM 쪽이 크기 때문으로 보입니다.
- **NTT 1회당 절감량 추정**: Mont.+Lazy ASYM N=4096에서 prime당 약 14,300사이클이 줄었으므로, NTT 1회당 약 4,800사이클입니다. 원본 butterfly 1쌍에서 `ldr` 2개가 빠진 효과(24,576쌍)와 복사 루프 2개 제거로 설명할 수 있는 규모입니다.
- **Cortex-M4(NTT 커널 1.75× → 3.91×)보다 효과가 훨씬 작은 이유**는 세 가지로 보입니다.
  1. 이번 측정 단위는 NTT 커널이 아니라 `ckks_encode_encrypt_*` 전체입니다.
  2. M1은 out-of-order 코어여서 반복되는 `ldr`(L1 적중)과 복사 루프의 지연을 상당 부분 숨깁니다.
  3. M4에서는 64 KiB 스택 버퍼와 복사의 메모리 대기 비용이 상대적으로 큽니다.
- **NTT 커널만의 M1 speedup은 측정하지 않았습니다.** 이 값이 필요하면 `ntt_inpl` 단독 마이크로벤치마크(같은 `m1cycles.c`, 같은 warm-up과 반복 횟수)를 추가로 측정해야 합니다. **[찾을 수 없음]**

---

## 6. 참고 사항

- **ASYM 측정 하네스의 기존 문제 [확인됨]**
  - `ckks_encode_encrypt_asym`이 `pk_c0/pk_c1`를 암호문으로 덮어씁니다. 그래서 1,000회 반복 중 2회차부터는 공개키가 아닌 값을 입력으로 씁니다.
  - 지난번 보고서 3.4절과 같은 문제이고, 이번에도 하네스는 수정하지 않았습니다.
  - 구성 간 비교 조건은 동일합니다.
- **빌드 트리 보존 [확인됨]**
  - 구성별 빌드 트리는 세션 스크래치패드(`/private/tmp/claude-501/.../scratchpad/trees_v2`)에 있어서 영구 보존되지 않습니다.
  - 재현하려면 `scripts/gen_trees.py <device 사본> <출력> meas|csv|verify`로 다시 만들면 됩니다.
  - 빌드는 `scripts/build_one.sh`를 씁니다.
- **기존 결과 [확인됨]**: `results/m1-fix-20261104-170935/`의 파일은 읽기만 했고 수정하지 않았습니다.

## 7. 결과 폴더 파일 목록

| 파일 | 내용 |
|---|---|
| `report.md` | 이 보고서 |
| `git_head_before.txt`, `git_status_before.txt`, `diff_before.patch` | 시작 상태(HEAD, status, diff) |
| `diff_after.patch`, `git_status_after.txt`, `new_file_ntt_signed_v2.patch` | 변경 후 `git diff`, status, 새 파일 내용 |
| `verify1_equivalence.csv` | 검증 1: 비트 동일성 |
| `verify2_summary.csv`, `verify_logs/` | 검증 2: 테스트 벡터 9개 |
| `verify3_asan.csv`, `asan/` | 검증 3: ASan |
| `disasm/` | 검증 4: 디스어셈블과 hot loop 비교 |
| `summary.csv`, `analysis_tables.md`, `meas_per_test_prime.csv`, `iter_cycles.csv` | 측정 집계와 반복별 사이클 |
| `compare_m1fix_vs_v2.csv` | m1-fix 대비 비교 |
| `raw/meas/`, `raw/csv/`, `raw/env.txt` | 원시 로그와 실행 환경 |
| `build_summary.txt` | 72개 빌드 결과(측정용·CSV용·검증용 × 24개 조건) |
| `run_measurements.sh`, `scripts/` | 실행 스크립트, 생성·빌드·검증·분석 스크립트(`gen_trees.py`, `build_one.sh`, `verify_one.sh`, `equiv_test.c`, `analyze_m1fix_original.py`, `compare_with_m1fix.py`) |
