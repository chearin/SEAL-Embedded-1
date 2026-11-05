# SEAL-Embedded CKKS: signed lazy NTT 입력 복사 범위 수정 후 Apple M1 재측정 보고서

- 작성일: 2026-11-04
- 결과 폴더: `results/m1-fix-20261104-170935/`
- 표기 규칙
  - **[확인됨]**: 이번 작업에서 직접 실행하거나 파일로 확인한 사실
  - **[추정]**: 근거는 있지만 직접 확인하지 못한 해석
  - **[찾을 수 없음]**: 찾아봤지만 존재를 확인하지 못한 항목

---

## 0. 요약

| 항목 | 결과 |
|---|---|
| 수정 | `device/lib/ntt.c:356` 한 줄만 수정했습니다(`i < max_n` → `i < n`). **[확인됨]** |
| 정확성 | 24개 조건 × 기본 테스트 벡터 9개를 모두 통과했습니다(N=4096은 prime 3개까지 포함해 27회). **[확인됨]** |
| 버그 실증 | 수정 전에는 ASan에서 `heap-buffer-overflow READ of size 65536`이 났고, 수정 후에는 0건입니다(Mont.+Lazy, 6개 조건). **[확인됨]** |
| 성능 | Baseline과 Barrett Mul은 논문 대비 −0.7%~+2.7%로 재현됐습니다. Mont.+Lazy NTT와 Integrated는 논문보다 1.3~13.3% 적은 사이클이 나왔고, speedup이 커졌습니다(예: Integrated ASYM N=1024는 1.31× → 1.54×). **[확인됨]** |
| M4 코드 | 이 저장소에는 Cortex-M4(ARMv7E-M)용 signed NTT 구현이 없습니다. **[찾을 수 없음]** (자세한 내용은 6절) |

---

## 1. 수정 전 상태 확인

### 1.1 저장소 상태 [확인됨]

- HEAD: `7aa1bf2d35ed3522101bf8dcbc07bac415e135fb` ("Update ARMv8 optimization and benchmark")
- 브랜치: `montgomery_armv8` (origin과 동기화된 상태)
- `git status`: 작업 트리 clean
- 원본 기록 파일: `git_state_before.txt`
- 수정 전 `git diff`: 0바이트 (`diff_before_fix.patch`)

### 1.2 버그 위치 [확인됨]

- 수정 대상: `device/lib/ntt.c:356`
  - 함수: `cr_signed_ntt_lazy_inpl()` (334행)
  - 위치: 27-bit prime 분기 안의 입력 복사 루프
  - 배경: 350행에서 `ZZsign signed_vec[max_n]`을 선언하고, `max_n = 16384`(349행)입니다.
- 같은 패턴이 하나 더 있습니다: `device/lib/ntt.c:258`의 `ntt_non_lazy_inpl_test_v1()`
  - 이 함수는 `ntt_inpl`에서 주석 처리되어 있어, 4개 구성 어디에서도 호출되지 않습니다.
  - 규칙("한 줄만 수정")에 따라 **수정하지 않았습니다.**

### 1.3 기존 M1 측정 방법

| 항목 | 내용 | 근거 |
|---|---|---|
| 빌드 | `device/build.sh`와 같은 방식입니다. `cmake -S . -B build -DSE_BUILD_LOCAL=ON -DCMAKE_BUILD_TYPE=Release` 후 `cmake --build build --verbose`로 빌드하고, `sudo ./build/bin/seal_embedded_tests`로 실행합니다. | [확인됨] |
| 컴파일러 | Apple clang 17.0.0 (clang-1700.4.4.1), arm64, macOS 15.6.1 (24G90), Apple M1 16 GB입니다. 논문 Table 2와 현재 장비가 같습니다. | [확인됨] |
| 컴파일 옵션 | `-O3 -DNDEBUG -std=gnu11 -arch arm64 -fPIC`, 정의는 `-DSE_DATA_PATH="adapter_output_data" -DSE_DATA_PATH_LEN=19 -DSE_UNDEF_ASSERT_FORCE`입니다. 기존 `device/build`의 `flags.make`와 이번 빌드가 일치합니다. | [확인됨] |
| 라이브러리 설정 | `user_defines.h` 기본값을 씁니다: `SE_USE_MALLOC`, NTT=one-shot(1), IFFT=OTF(0), `SE_DEFAULT_4K_27BIT`. 따라서 모든 N에서 27-bit prime을 씁니다: 1024와 2048은 134012929, 4096은 134012929/134111233/134176769. | [확인됨] |
| 4개 구성 전환 | 소스의 주석을 켜고 끄는 방식입니다. ① `lib/ntt.c`의 `ntt_inpl()`에서 `ntt_non_lazy_inpl`과 `cr_signed_ntt_lazy_inpl` 중 하나를 고릅니다. ② `lib/uintmodarith.h`의 `mul_mod()`에서 원래 `mul_uint_wide`+`barrett_reduce_wide`와 `cr_barrett_mul_asm` 중 하나를 고릅니다. Baseline=①원본+②원본, Barrett Mul=①원본+②asm, Mont.+Lazy=①lazy+②원본, Integrated=①lazy+②asm입니다. | 전환 지점은 [확인됨]. 이 조합이 논문의 4개 구성과 같다는 해석은 [추정]입니다(논문 Table 4의 정의, HEAD=Baseline 상태, 직전 커밋 `2636eae`=Integrated 상태와 일치). |
| N / SYM·ASYM 선택 | `test/main.c`의 `n, nprimes` 줄과 `test_ckks_encode_encrypt_sym/asym` 호출 줄을 주석으로 전환합니다. | [확인됨] |
| 사이클 계측 | `m1cycles.c`(dougallj 방식)를 씁니다. 이 코드는 kperf private framework를 `dlopen`하고, 설정 레지스터 0에 `CPMU_CORE_CYCLE \| CFGWORD_EL0A64EN_MASK`를 걸고, QoS를 `USER_INTERACTIVE`(P코어)로 둡니다. `rdtsc()`는 `kpc_get_thread_counters` 결과의 `g_counters[2]`를 반환합니다. 실행에 root가 필요합니다. | [확인됨] |
| 측정 경계 | `test/ckks_tests_{sym,asym}.c` 안에서 `ckks_encode_encrypt_sym` 또는 `ckks_encode_encrypt_asym` 1회 호출을 앞뒤 `rdtsc()`로 감쌉니다. | [확인됨] |
| warm-up / 반복 | 각 (테스트 벡터, prime)마다 측정 대상 함수를 5회 먼저 호출하고, `setup_rdtsc()` 후 1,000회 측정합니다. 출력값은 `sum/1000`(정수 나눗셈)입니다. | [확인됨] |
| 입력 | 테스트 루프가 기본 테스트 벡터 9개(`set_encode_encrypt_test` 0~8)를 차례로 돌며, 각 벡터마다 위 측정을 하고 평균을 출력합니다. 비밀키는 `adapter_output_data/sk_N.dat`에서 읽고, 잡음은 `getrandom()`으로 만듭니다. | [확인됨] |
| N=4096 평균 방식 | prime 3개 각각에 대해 1,000회 평균을 구한 뒤, 세 값의 산술평균을 대표값으로 씁니다(논문 VII-D절). | [확인됨] (논문 기술 기준) |
| 9개 벡터 집계 방식 | 논문에는 9개 벡터 출력값을 어떻게 하나로 모았는지 적혀 있지 않습니다. 이 보고서는 **9개 벡터 평균(주 지표)**과 **Test 0만(보조 지표)**을 모두 계산했습니다. Baseline 재측정값이 주 지표 기준으로 논문과 ±1.9% 이내라서, 9개 평균 방식이었을 가능성이 높아 보입니다. | [추정] |
| 정확성 검사 상태 | 현재 측정 하네스에서는 `check_decode_decrypt_inpl` 호출이 주석 처리돼 있습니다. 그 내부의 `compare_poly`와 `se_assert(!err)`도 꺼져 있고, Release에서는 assert 자체가 비활성입니다. 측정 바이너리는 정확성을 검사하지 않는다는 뜻입니다. | [확인됨] |

---

## 2. 수정 diff [확인됨]

`diff_after_fix.patch` 내용입니다. 저장소에서 바뀐 것은 이 한 줄뿐입니다.

```diff
diff --git a/device/lib/ntt.c b/device/lib/ntt.c
index c01180c..10c08e3 100644
--- a/device/lib/ntt.c
+++ b/device/lib/ntt.c
@@ -353,7 +353,7 @@ void cr_signed_ntt_lazy_inpl(const Parms *parms, const ZZ *ntt_roots, ZZ *vec)
         ZZsign u, v;
         // todo signed represetation으로 변경 + lazy reduction
         //* ntt loop 3개
-        for (size_t i = 0; i < max_n; i++)
+        for (size_t i = 0; i < n; i++)
         {
             signed_vec[i] = (ZZsign)vec[i];  // changes from unsigned to signed representation
         }
```

- 작업 후 저장소 상태 [확인됨]
  - HEAD는 그대로 `7aa1bf2`, 브랜치는 그대로 `montgomery_armv8`입니다.
  - `git status`에는 ` M device/lib/ntt.c`와 `?? results/`만 있습니다.
  - stash는 0개이고, 커밋은 하지 않았습니다.
- 4개 구성 전환과 `main.c`의 N·모드 전환은 **저장소 밖 사본**(스크래치패드)에서만 적용했습니다. [확인됨]
  - 생성 스크립트: `scripts/gen_trees.py`
  - 구성별 패치: `scripts/config_*.patch`
  - 저장소 파일은 위 한 줄 외에는 바꾸지 않았습니다.

---

## 3. 빌드와 정확성 검증

### 3.1 빌드 [확인됨]

- 4개 구성 × SYM/ASYM × N 3개 = 24개 조건을 빌드했습니다.
- 변형 4종(측정용, 반복별 CSV용, 검증용, 반복 후 검증용)까지 합쳐 총 96개 빌드가 모두 성공했습니다(`build_summary.txt`).
- 컴파일 경고 `-Wasm-operand-widths`(`uintmodarith.h:178`, `:239`)는 수정 전 코드에서도 나던 경고입니다.

### 3.2 기본 테스트 벡터 9개 검증 [확인됨]

**검증 방법**

- 검증용 바이너리에서 SEAL-Embedded 원래 테스트 방식대로 prime마다 1회 암호화한 뒤, 꺼져 있던 검사를 다시 켰습니다.
  - `check_decode_decrypt_inpl` 호출
  - `compare_poly(pte_calc, decrypted)`: 복호 결과가 계산된 m+e와 **정확히 일치**하는지 확인
  - `se_assert(!err)`: decode 값이 원래 메시지와 **오차 0.1 미만**인지 확인
- 최적화는 `-O3` 그대로 두고, `-USE_UNDEF_ASSERT_FORCE`로 assert만 켰습니다.

**결과** (`verify_summary.csv`, 로그는 `verify_logs/`)

| 구성 | SYM 1024 | SYM 2048 | SYM 4096 | ASYM 1024 | ASYM 2048 | ASYM 4096 |
|---|---|---|---|---|---|---|
| Baseline | 9/9 | 9/9 | 27/27 | 9/9 | 9/9 | 27/27 |
| Barrett Mul | 9/9 | 9/9 | 27/27 | 9/9 | 9/9 | 27/27 |
| Mont.+Lazy NTT | 9/9 | 9/9 | 27/27 | 9/9 | 9/9 | 27/27 |
| Integrated | 9/9 | 9/9 | 27/27 | 9/9 | 9/9 | 27/27 |

- 24개 조건 모두 종료 코드 0, 불일치 0건, "All tests passed."가 출력됐습니다.
- N=4096은 벡터 9개 × prime 3개 = 27회입니다.

### 3.3 수정 전후 메모리 오류 확인 (AddressSanitizer) [확인됨]

- 대상: Mont.+Lazy NTT 구성, SYM/ASYM × N 3개 = 6개 조건
- 빌드 옵션: `-O1 -g -fsanitize=address`
- 로그: `asan/`

| | 수정 전 (`max_n`) | 수정 후 (`n`) |
|---|---|---|
| ASan 오류 | 6/6 조건 모두 `heap-buffer-overflow READ of size 65536` (`cr_signed_ntt_lazy_inpl ntt.c:358`) | 0/6 |
| 검증 통과 | ASan이 첫 NTT에서 중단 | 6/6 모두 통과 |

- 예시: N=1024 SYM에서는 `ckks_mempool_setup_sym`이 `calloc`한 24,832바이트 영역 바로 뒤에서부터 읽습니다.
- N=4096에서도 mempool이 16384 워드(64 KiB)보다 작아서 범위 밖을 읽습니다. 즉 이 버그는 N < 16384인 모든 설정에서 발생합니다.

### 3.4 기존 측정 하네스에서 발견한 별도 문제 (이번 작업에서는 수정하지 않음) [확인됨]

- **현상**: 측정 순서 그대로(warm-up 5회 + 1,000회) 반복한 뒤 마지막 출력을 검사하면, **ASYM은 Baseline을 포함한 4개 구성 모두** `compare_poly`에서 실패합니다. SYM은 모두 통과합니다(`verify_summary.csv`의 `verifyloop` 행).
- **원인**: `ckks_encode_encrypt_asym`이 입력 공개키 버퍼 `pk_c0/pk_c1`에 암호문을 **덮어씁니다**. 그래서 2회차부터는 공개키가 아니라 이전 암호문을 입력으로 받습니다.
- **영향**
  - 이 수정과는 관계가 없고, 모든 구성에 똑같이 적용됩니다.
  - 연산 경로와 연산량은 데이터와 무관하게 같으므로, 사이클 비교 자체에 미치는 영향은 작을 것으로 봅니다. **[추정]**
  - 다만 측정된 ASYM 반복이 "유효한 암호화"는 아니라는 점은 논문의 측정 방법 서술에 반영하거나, 하네스에서 매 반복 전에 pk를 다시 불러오도록 고치는 것을 검토해 볼 만합니다.

---

## 4. 측정 결과 (24개 조건) [확인됨]

**측정 조건**

- 측정용 바이너리는 기존 하네스를 한 글자도 바꾸지 않았습니다(warm-up 5회, 1,000회, `m1cycles.c`, `-O3`).
- `sudo`로 1회씩 순차 실행했습니다. 실행 환경(`raw/env.txt`): AC 전원(충전 중), load average 약 1.4~2.6
- 원시 로그: `raw/meas/*.log`

**주 지표**

- 각 테스트 벡터의 1,000회 평균을 prime별로 구합니다.
- N=4096이면 prime 3개를 산술평균합니다.
- 그 값을 9개 벡터에 대해 다시 평균합니다.
- 괄호 안은 Baseline 대비 speedup(= C_baseline / C_opt)입니다.

| Configuration | Mode | N = 1024 | N = 2048 | N = 4096 |
|---|---|---:|---:|---:|
| Baseline | SYM | 116,101 | 236,593 | 477,749 |
| | ASYM | 110,290 | 229,320 | 477,813 |
| Barrett Mul | SYM | 113,986 (1.02×) | 233,270 (1.01×) | 479,058 (1.00×) |
| | ASYM | 109,915 (1.00×) | 232,106 (0.99×) | 495,540 (0.96×) |
| Mont.+Lazy NTT | SYM | 96,620 (1.20×) | 193,797 (1.22×) | 386,600 (1.24×) |
| | ASYM | 81,867 (1.35×) | 165,691 (1.38×) | 341,461 (1.40×) |
| Integrated | SYM | 87,462 (1.33×) | 175,598 (1.35×) | 349,640 (1.37×) |
| | ASYM | 71,755 (1.54×) | 147,711 (1.55×) | 307,745 (1.55×) |

**보조 지표: Test 0만 사용** (논문이 "고정된 1개 입력"을 썼을 가능성에 대비해 함께 제시합니다)

| Configuration | Mode | N = 1024 | N = 2048 | N = 4096 |
|---|---|---:|---:|---:|
| Baseline | SYM | 127,320 | 253,107 | 481,809 |
| | ASYM | 126,714 | 246,184 | 481,697 |
| Barrett Mul | SYM | 130,256 (0.98×) | 241,861 (1.05×) | 483,076 (1.00×) |
| | ASYM | 128,569 (0.99×) | 244,753 (1.01×) | 497,857 (0.97×) |
| Mont.+Lazy NTT | SYM | 104,274 (1.22×) | 203,852 (1.24×) | 390,720 (1.23×) |
| | ASYM | 99,773 (1.27×) | 176,225 (1.40×) | 341,477 (1.41×) |
| Integrated | SYM | 96,968 (1.31×) | 187,654 (1.35×) | 353,172 (1.36×) |
| | ASYM | 81,513 (1.55×) | 157,788 (1.56×) | 308,417 (1.56×) |

**Test 0 지표에 대한 참고**

- Test 0은 각 실행에서 가장 먼저 측정되는 벡터입니다. 그래서 N=1024/2048에서는 나머지 벡터보다 5~25% 높게 나옵니다(9개 벡터 간 편차는 `meas_per_test_prime.csv`에 있습니다).
- N=4096에서는 벡터 간 편차가 ≤1.3%로 작습니다.
- 이 차이는 첫 측정 구간의 시스템 상태(캐시, 주파수 등) 때문으로 보입니다. **[추정]**
- 그래서 주 지표로는 9개 벡터 평균을 권합니다.

### 4.1 반복별 개별 사이클 (CSV) [확인됨]

**저장 방식**

- 파일: `iter_cycles.csv` (12.7 MB, 행 = config, mode, N, test, prime_idx, iter, cycles)
- 바이너리 24개에서 총 360,000개 값입니다.
- 측정 구간 **밖에서** `end - start` 값을 배열에 저장하는 줄 하나만 추가한 별도 바이너리로 수집했습니다.
- 평균값 표(위)는 원래 하네스 바이너리에서 따로 얻은 값이며, 두 실행의 평균은 0.01~1.46% 이내로 일치합니다(`summary.csv`).

| Configuration | Mode | N | 표본 수 | 평균 | 중앙값 | 표준편차 | min | p5 | p95 | max |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Baseline | SYM | 1024 | 9,000 | 116,629 | 113,877 | 15,813 | 111,466 | 111,735 | 119,373 | 258,245 |
| Baseline | SYM | 2048 | 9,000 | 236,687 | 234,460 | 23,736 | 228,399 | 230,475 | 240,360 | 527,652 |
| Baseline | SYM | 4096 | 27,000 | 477,447 | 475,846 | 17,762 | 469,280 | 470,025 | 487,032 | 1,095,270 |
| Baseline | ASYM | 1024 | 9,000 | 109,393 | 108,227 | 13,726 | 107,961 | 108,099 | 108,393 | 275,115 |
| Baseline | ASYM | 2048 | 9,000 | 229,432 | 227,347 | 26,730 | 226,882 | 227,143 | 227,807 | 585,851 |
| Baseline | ASYM | 4096 | 27,000 | 477,663 | 477,251 | 15,787 | 476,184 | 476,807 | 477,987 | 1,242,867 |
| Barrett Mul | SYM | 1024 | 9,000 | 112,319 | 110,072 | 13,807 | 107,569 | 107,909 | 115,515 | 253,098 |
| Barrett Mul | SYM | 2048 | 9,000 | 232,370 | 230,001 | 24,881 | 223,806 | 225,934 | 236,048 | 534,132 |
| Barrett Mul | SYM | 4096 | 27,000 | 474,833 | 473,428 | 15,380 | 466,802 | 467,661 | 484,565 | 1,107,183 |
| Barrett Mul | ASYM | 1024 | 9,000 | 109,242 | 107,447 | 17,185 | 107,018 | 107,277 | 107,659 | 276,655 |
| Barrett Mul | ASYM | 2048 | 9,000 | 233,179 | 230,713 | 29,656 | 229,978 | 230,360 | 231,207 | 604,176 |
| Barrett Mul | ASYM | 4096 | 27,000 | 494,977 | 494,442 | 18,745 | 492,216 | 493,363 | 495,880 | 1,310,754 |
| Mont.+Lazy NTT | SYM | 1024 | 9,000 | 96,869 | 94,794 | 9,937 | 92,310 | 92,611 | 100,290 | 185,561 |
| Mont.+Lazy NTT | SYM | 2048 | 9,000 | 194,729 | 192,486 | 18,768 | 186,345 | 188,413 | 199,464 | 377,203 |
| Mont.+Lazy NTT | SYM | 4096 | 27,000 | 386,336 | 384,820 | 13,586 | 378,143 | 378,987 | 395,950 | 754,295 |
| Mont.+Lazy NTT | ASYM | 1024 | 9,000 | 81,029 | 79,587 | 11,162 | 79,299 | 79,444 | 79,789 | 173,221 |
| Mont.+Lazy NTT | ASYM | 2048 | 9,000 | 165,677 | 164,340 | 15,539 | 163,751 | 164,078 | 164,690 | 354,315 |
| Mont.+Lazy NTT | ASYM | 4096 | 27,000 | 341,730 | 341,213 | 13,160 | 340,035 | 340,660 | 341,945 | 741,716 |
| Integrated | SYM | 1024 | 9,000 | 88,134 | 85,373 | 12,521 | 82,886 | 83,229 | 90,895 | 179,470 |
| Integrated | SYM | 2048 | 9,000 | 175,383 | 173,777 | 15,542 | 167,503 | 169,712 | 179,694 | 358,539 |
| Integrated | SYM | 4096 | 27,000 | 348,846 | 347,292 | 13,666 | 340,672 | 341,479 | 358,534 | 723,741 |
| Integrated | ASYM | 1024 | 9,000 | 72,238 | 70,500 | 12,135 | 70,214 | 70,366 | 70,700 | 162,283 |
| Integrated | ASYM | 2048 | 9,000 | 148,165 | 146,345 | 17,988 | 145,740 | 146,021 | 146,924 | 335,744 |
| Integrated | ASYM | 4096 | 27,000 | 308,783 | 308,059 | 14,786 | 304,787 | 307,000 | 309,612 | 698,867 |

- 분포가 오른쪽으로 길게 늘어져 있습니다. max가 중앙값의 약 2배인 이상치(인터럽트나 컨텍스트 스위치로 보임)가 평균을 약간 끌어올립니다.
- 중앙값으로 비교해도 구성 간 순위와 speedup 경향은 같습니다.

---

## 5. 기존 M1 결과(논문)와 비교

**비교 기준**

- 기준 문서: `~/Downloads/2026_IEEE_Access_CKKS_ARMv7_8__채린__영범_ (1).pdf` (10/1 17:05본) **[확인됨]**
- 이 버전에서는 **Apple M1 결과가 Table 5**("Average Per-RNS-Prime Core-Encryption Performance on Apple M1")이고, **Table 6은 Cortex-M4**입니다. 요청하신 "Table 6의 Apple M1 부분"은 이 PDF의 Table 5로 보고 비교했습니다. 다른 원고 버전에서 표 번호가 다를 수 있습니다. **[확인됨]**
- 같은 이름의 16:36본 PDF는 45바이트가 달라 별도로 대조하지 않았습니다.

재측정값은 4절의 주 지표(9개 벡터 평균)입니다. 차이 = (재측정 − 논문) / 논문입니다.

| Configuration | Mode | N | 논문 (Table 5) | 논문 speedup | 재측정 (수정 후) | 재측정 speedup | 사이클 차이 |
|---|---|---:|---:|---:|---:|---:|---:|
| Baseline | SYM | 1024 | 116,898 | — | 116,101 | — | −0.7% |
| Baseline | SYM | 2048 | 234,478 | — | 236,593 | — | +0.9% |
| Baseline | SYM | 4096 | 477,361 | — | 477,749 | — | +0.1% |
| Baseline | ASYM | 1024 | 108,229 | — | 110,290 | — | +1.9% |
| Baseline | ASYM | 2048 | 227,205 | — | 229,320 | — | +0.9% |
| Baseline | ASYM | 4096 | 477,284 | — | 477,813 | — | +0.1% |
| Barrett Mul | SYM | 1024 | 111,006 | 1.05× | 113,986 | 1.02× | +2.7% |
| Barrett Mul | SYM | 2048 | 230,250 | 1.02× | 233,270 | 1.01× | +1.3% |
| Barrett Mul | SYM | 4096 | 474,964 | 1.01× | 479,058 | 1.00× | +0.9% |
| Barrett Mul | ASYM | 1024 | 107,754 | 1.00× | 109,915 | 1.00× | +2.0% |
| Barrett Mul | ASYM | 2048 | 231,106 | 0.98× | 232,106 | 0.99× | +0.4% |
| Barrett Mul | ASYM | 4096 | 495,204 | 0.96× | 495,540 | 0.96× | +0.1% |
| Mont.+Lazy NTT | SYM | 1024 | 105,409 | 1.11× | 96,620 | **1.20×** | −8.3% |
| Mont.+Lazy NTT | SYM | 2048 | 197,776 | 1.19× | 193,797 | **1.22×** | −2.0% |
| Mont.+Lazy NTT | SYM | 4096 | 391,516 | 1.22× | 386,600 | **1.24×** | −1.3% |
| Mont.+Lazy NTT | ASYM | 1024 | 88,765 | 1.22× | 81,867 | **1.35×** | −7.8% |
| Mont.+Lazy NTT | ASYM | 2048 | 178,091 | 1.28× | 165,691 | **1.38×** | −7.0% |
| Mont.+Lazy NTT | ASYM | 4096 | 350,014 | 1.36× | 341,461 | **1.40×** | −2.4% |
| Integrated | SYM | 1024 | 95,534 | 1.22× | 87,462 | **1.33×** | −8.4% |
| Integrated | SYM | 2048 | 182,711 | 1.28× | 175,598 | **1.35×** | −3.9% |
| Integrated | SYM | 4096 | 354,126 | 1.35× | 349,640 | **1.37×** | −1.3% |
| Integrated | ASYM | 1024 | 82,778 | 1.31× | 71,755 | **1.54×** | −13.3% |
| Integrated | ASYM | 2048 | 161,396 | 1.41× | 147,711 | **1.55×** | −8.5% |
| Integrated | ASYM | 4096 | 315,883 | 1.51× | 307,745 | **1.55×** | −2.6% |

### 해석

**[확인됨]**

- 이번 수정은 `cr_signed_ntt_lazy_inpl`만 바꿉니다. 그래서 이 함수를 쓰지 않는 Baseline과 Barrett Mul은 논문값과 −0.7%~+2.7% 차이로 재현됐습니다. 측정 환경과 방법이 기존과 같다는 근거가 됩니다.
- lazy NTT를 쓰는 두 구성(Mont.+Lazy NTT, Integrated)은 12개 조건 모두에서 사이클이 줄었습니다.

**[추정]**

- 사이클이 줄어든 원인은 NTT 1회마다 하던 16384개 복사(64 KiB 읽기)가 N개 복사로 줄어든 것으로 보입니다. 다음 세 가지가 이 설명과 맞습니다.
  - **N이 작을수록 개선 폭이 큽니다.** 제거되는 불필요 복사는 N=1024에서 15,360개, N=4096에서 12,288개이고, 전체 사이클에서 차지하는 비중은 N이 작을수록 큽니다.
  - **ASYM이 SYM보다 개선 폭이 큽니다.** ASYM 경로는 prime마다 `ntt_inpl`을 3회(u, e1, m+e0) 호출하고, SYM 경로는 2회(s, m+e) 호출합니다(`ckks_asym.c:242,265,282`, `ckks_sym.c:266,306`).
  - **Integrated ASYM N=1024의 감소폭이 가장 큽니다.** 이 조건이 −13.3%(1.31× → 1.54×)로 가장 크게 줄었습니다.
- 결과적으로 수정 후에는 speedup이 N에 거의 무관하게 나옵니다(Integrated ASYM: 1.54×, 1.55×, 1.55×).
  - 논문 VIII-A절의 "NTT optimization의 효과는 polynomial degree가 증가함에 따라 보다 뚜렷하게 나타났다"는 서술 중 일부는 이 고정 비용(16384 복사)이 만든 착시였을 가능성이 있습니다.
  - 논문 본문과 수치를 갱신할 때 검토가 필요합니다.

---

## 6. Cortex-M4 코드에 같은 문제가 있는지

**이 저장소 안: ARMv7E-M용 signed NTT 구현은 [찾을 수 없음]**

모든 로컬 브랜치와 원격 브랜치를 `git grep`으로 검사했습니다.

- `origin/montgomery_m4` 브랜치 **[확인됨]**
  - `ntt.c`에 `cr_signed_ntt_lazy_inpl`과 `max_n` 루프가 없습니다.
  - `barrett.S`와 `uintmodarith.h`의 asm은 `x`/`w` 레지스터를 쓰는 **AArch64 코드**이고, Thumb-2(ARMv7E-M) 코드가 아닙니다.
- `main`, `barrett_armv8`, `origin/contrib`: 해당 함수가 없습니다. **[확인됨]**
- `origin/raspberry`: 같은 버그(`ntt.c:258`, `:356`)가 있지만, 이 브랜치는 ARMv8(라즈베리파이)용입니다. **[확인됨]**
- 논문 V-C절이 설명하는 M4 구현(SMULL + ADDS/ADC 기반 Montgomery)은 이 Mac 어디에서도 찾지 못했습니다. **[찾을 수 없음]**
  - 메모상 M4 측정은 STM32CubeIDE(Windows/리눅스 환경)에서 했으므로, 그 소스는 다른 PC에 있을 것으로 보입니다. **[추정]**

**저장소 밖 같은 계열 사본: 같은 C 루프가 있습니다 [확인됨]**

- `~/Desktop/git/Embedded-SEAL_mont/device/lib/ntt.c:248, :342`
- `~/Desktop/git/SEAL-Embedded_mont_armv8/device/lib/ntt.c:248, :342`
- `~/Desktop/git/SEAL-Embedded_param_armv8{,CR}/device/lib/ntt.c:258, :356`
- `~/Downloads/ntt.c:248, :342`
- 다만 이 사본들의 asm도 AArch64이므로, M4 빌드에 쓰인 코드인지는 확인할 수 없습니다.

**M4에서의 영향 [추정]**

- M4 구현이 같은 C 코드(`signed_vec[16384]` VLA + `max_n` 복사)에서 출발했다면 M4에도 같은 문제가 있습니다.
- M4에서는 영향이 더 클 수 있습니다.
  - MMU가 없어서 범위 밖 읽기가 오류 없이 지나가므로, 정확성 테스트로는 드러나지 않습니다.
  - `signed_vec[16384]`(64 KiB) 스택 배열은 NUCLEO-L4R5ZI(SRAM 640 KB)의 스택 예산에 큰 부담입니다.
  - 매 NTT마다 64 KiB를 읽는 비용이 논문 Table 6의 Mont.+Lazy와 Integrated 수치에 포함됐을 수 있습니다.
- M4 소스를 확보해 `for (... < max_n ...)` 패턴이 있는지 확인하고, 있다면 M4도 재측정할 것을 권합니다.

---

## 7. 결과 폴더 파일 목록

| 파일 | 내용 |
|---|---|
| `report.md` | 이 보고서 |
| `git_state_before.txt`, `git_state_after.txt` | 수정 전후 HEAD/branch/status |
| `diff_before_fix.patch` (0 B), `diff_after_fix.patch` | 수정 전후 `git diff` |
| `summary.csv` | 24개 조건의 평균(9개 평균 / Test 0), speedup, 논문값, CSV 통계 |
| `meas_per_test_prime.csv` | 조건 × 테스트 벡터 × prime별 1,000회 평균(원 하네스 출력) |
| `iter_cycles.csv` | 반복별 개별 사이클 360,000개 |
| `raw/meas/*.log`, `raw/csv/*.log`, `raw/env.txt` | 측정 원시 출력과 실행 환경 |
| `verify_summary.csv`, `verify_logs/` | 정확성 검증 결과(`verify`=원래 테스트 방식, `verifyloop`=측정 순서 그대로) |
| `asan/` | 수정 전후 AddressSanitizer 로그 |
| `build_summary.txt` | 96개 빌드 결과 |
| `scripts/` | `gen_trees.py`(구성 트리 생성), `config_*.patch`(구성 전환 내용), `build_one.sh`, `verify_one.sh`, `analyze.py`; 측정 실행은 `run_measurements.sh` |

**재현 시 주의** [확인됨]

- 빌드 트리는 세션 스크래치패드(`/private/tmp/claude-501/...`)에 있어서 영구 보존되지 않습니다.
- 재현하려면 `scripts/gen_trees.py`로 트리를 다시 만들어야 합니다.
  - 입력: 수정 후 `device/` 사본 + `adapter_output_data/`
  - 변형 인자: `meas|csv|verify|verifyloop`
