# 원격 v2 소스 동등성 확인

원격 기준 커밋: `905228e5cad0d98e698338b766e648bc195c75b4`. 원본 RPi 작업 트리는 변경하지 않고 /home/cselab/v2check에서 검사했다. [확인됨]

GCC 12.2.0 및 측정 때와 같은 -O3 -mcpu=cortex-a76 -fno-strict-aliasing -fPIC -std=gnu11, 기존 defines/헤더를 사용했다. 원격 함수는 충돌 방지를 위해 remote_v2로 이름만 분리했다. 명령과 원시 로그는 v2check/commands.json 및 validation.log에 보존했다. [확인됨]

N=1024/2048/4096 × q=134012929/134111233/134176769 × (무작위 1000 + 경계 8), 총 9072개 벡터에서 원격 v2, 측정에 사용한 작업 트리 v2, 원본 signed lazy NTT의 출력이 모두 비트 단위로 일치했다. 기존 validate-v2.c의 seed, 입력 생성, root 생성 방식과 산술 검사 순서를 유지했다. [확인됨]

```text
PASS N=1024 q=134012929 random=1000 boundary=8 arithmetic=100008
PASS N=1024 q=134111233 random=1000 boundary=8 arithmetic=100008
PASS N=1024 q=134176769 random=1000 boundary=8 arithmetic=100008
PASS N=2048 q=134012929 random=1000 boundary=8 arithmetic=100008
PASS N=2048 q=134111233 random=1000 boundary=8 arithmetic=100008
PASS N=2048 q=134176769 random=1000 boundary=8 arithmetic=100008
PASS N=4096 q=134012929 random=1000 boundary=8 arithmetic=100008
PASS N=4096 q=134111233 random=1000 boundary=8 arithmetic=100008
PASS N=4096 q=134176769 random=1000 boundary=8 arithmetic=100008
ALL PASS
```

## Butterfly hot loop 대조

원격 object의 0x84–0xb4와 측정 ELF의 0x120124–0x120154를 비교했다. load부터 back branch까지 양쪽 13개 명령이며 종류·개수·순서가 같다. 레지스터 번호는 다르다. [확인됨]

| 순서 | 측정 RPi v2 | 원격 v2 |
|---:|---|---|
| 1 | `ldr w1, [x0]` | `ldr w1, [x0]` |
| 2 | `ldr w3, [x0, x21, lsl #2]` | `ldr w3, [x0, x7, lsl #2]` |
| 3 | `smull x6, w3, w4` | `smull x9, w3, w6` |
| 4 | `mul w7, w6, w20` | `mul w10, w9, w5` |
| 5 | `smaddl x2, w7, w19, x6` | `smaddl x2, w10, w4, x9` |
| 6 | `asr x2, x2, #32` | `asr x2, x2, #32` |
| 7 | `add w3, w1, w2` | `add w3, w1, w2` |
| 8 | `sub w1, w1, w2` | `sub w1, w1, w2` |
| 9 | `str w3, [x0]` | `str w3, [x0]` |
| 10 | `str w1, [x0, x21, lsl #2]` | `str w1, [x0, x7, lsl #2]` |
| 11 | `add x0, x0, #0x4` | `add x0, x0, #0x4` |
| 12 | `cmp x5, x0` | `cmp x8, x0` |
| 13 | `b.ne 120124 <cr_signed_ntt_lazy_inpl_v2+0x124>` | `b.ne 84 <remote_v2+0x84>` |

함수 전체의 진입부·종료부·레지스터 저장·코드 배치는 동일하지 않다. 이 검사는 기능 및 hot-loop 명령 순서의 동등성 확인이며, 원격 소스를 다시 성능 측정한 결과는 아니다. 기존 report.md와 CSV는 2026-11-05 RPi 소스/ELF에서 얻은 값을 그대로 보존했다. [확인됨]
