| 원본 | v2 |
|---|---|
| `c0830:	b8617a82 	ldr	w2, [x20, x1, lsl #2]` | `120124:	b9400001 	ldr	w1, [x0]` |
| `c0834:	b8617905 	ldr	w5, [x8, x1, lsl #2]` | `120128:	b8757803 	ldr	w3, [x0, x21, lsl #2]` |
| `c0838:	b9400263 	ldr	w3, [x19]` | `12012c:	9b247c66 	smull	x6, w3, w4` |
| `c083c:	b9400e64 	ldr	w4, [x19, #12]` | `120130:	1b147cc7 	mul	w7, w6, w20` |
| `c0840:	9b2a7ca6 	smull	x6, w5, w10` | `120134:	9b3318e2 	smaddl	x2, w7, w19, x6` |
| `c0844:	9b047cc7 	mul	x7, x6, x4` | `120138:	9360fc42 	asr	x2, x2, #32` |
| `c0848:	9b2318e5 	smaddl	x5, w7, w3, x6` | `12013c:	0b020023 	add	w3, w1, w2` |
| `c084c:	d360fca5 	lsr	x5, x5, #32` | `120140:	4b020021 	sub	w1, w1, w2` |
| `c0850:	0b050040 	add	w0, w2, w5` | `120144:	b9000003 	str	w3, [x0]` |
| `c0854:	4b050042 	sub	w2, w2, w5` | `120148:	b8357801 	str	w1, [x0, x21, lsl #2]` |
| `c0858:	b8217a80 	str	w0, [x20, x1, lsl #2]` | `12014c:	91001000 	add	x0, x0, #0x4` |
| `c085c:	b8217902 	str	w2, [x8, x1, lsl #2]` | `120150:	eb0000bf 	cmp	x5, x0` |
| `c0860:	91000421 	add	x1, x1, #0x1` | `120154:	54fffe81 	b.ne	120124 <cr_signed_ntt_lazy_inpl_v2+0x124>  // b.any` |
| `c0864:	eb09003f 	cmp	x1, x9` | `` |
| `c0868:	54fffe41 	b.ne	c0830 <v4_cr_signed_ntt_lazy_inpl+0x160>  // b.any` | `` |
