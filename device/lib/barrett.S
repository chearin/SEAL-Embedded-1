.text
.global cr_barrett_mul_asm      ; 심볼을 전역으로 노출

;static inline ZZ cr_barrett_mul(ZZ a, ZZ b, const Modulus *st_q)
.align 4
cr_barrett_mul_asm:
    ; Load
    ; x0 : a
    ; x1 : b
    ; x2 : st_q
    ldr w3, [x2, #0]    ; x3 = q
    ldr w4, [x2, #4]    ; x4 = const_ratio[0]
    ldr w5, [x2, #8]    ; x5 = const_ratio[1]

    ;precompute
    ;v = ((uint64_t)(st_q->const_ratio[0]) * b)>>32;
    mul x6, x4, x1
    lsr x6, x6, #32
    ;v += (st_q->const_ratio[1]) * b;
    madd x6, x5, x1, x6

    ;mod mul
    ;z = a * b;
    mul x7, x0, x1          ; x7 = z = a * b
    ;t = ((uint64_t)a * v) >> 32;
    mul x9, x0, x6    ;반올림 안됨
    lsr x9, x9, #32
    ;z = z - t*q;
    msub x7, x9, x3, x7

    ;if(z >= q) z -= q;
    subs x10, x7, x3     ; 언더플로우 발생시 cc(carry clear)가 0
    csel x0, x10, x7, hs ; cc가 1이면 x0=x8, 0이면 x0=x7
   
    ret                          ; x0에 담긴 값을 반환
