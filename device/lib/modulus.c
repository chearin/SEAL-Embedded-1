// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

/**
@file modulus.c
*/

#include "modulus.h"

#include <stdint.h>  // uint64_t, UINT64_MAX
#include <string.h>  // memcpy

#include "defines.h"
#include "util_print.h"

void set_modulus_custom(const ZZ q, ZZ hw, ZZ lw, ZZ m_invQ, ZZ r, ZZ r2, Modulus *mod)
{
    mod->value          = q;
    mod->const_ratio[1] = hw;
    mod->const_ratio[0] = lw;
    mod->inv_q          = m_invQ;
    mod->R             = r;
    mod->R2             = r2;
}

bool set_modulus(const uint32_t q, Modulus *mod)
{
    switch (q)
    {
        // -- Add cases for custom primes here

        // -- 27 bit primes
        case 134176769: set_modulus_custom(q, 0x20, 0x2802e03, 2751422463, 1310688, 37859837, mod); return 1;
        case 134111233: set_modulus_custom(q, 0x20, 0x6814e43, 1677615103, 3407840, 11243965, mod); return 1;
        case 134012929: set_modulus_custom(q, 0x20, 0xc84dfe5, 1140645887, 6553568, 119980059, mod); return 1;

        // -- 30-bit primes
        case 1062535169: set_modulus_custom(q, 0x4, 0xaccdb49, 1062535167, 0, 787883191, mod); return 1;
        case 1062469633: set_modulus_custom(q, 0x4, 0xadd3267, 1062469631, 0, 307678617, mod); return 1;
        case 1061093377: set_modulus_custom(q, 0x4, 0xc34cf30, 1061093375, 0, 653996240, mod); return 1;
        case 1060765697: set_modulus_custom(q, 0x4, 0xc86c0d4, 1060765695, 0, 393297708, mod); return 1;
        case 1060700161: set_modulus_custom(q, 0x4, 0xc9725e9, 1060700159, 0, 730323479, mod); return 1;
        case 1060175873: set_modulus_custom(q, 0x4, 0xd1a6142, 1060175871, 0, 390307518, mod); return 1;
        case 1058209793: set_modulus_custom(q, 0x4, 0xf07a84a, 1058209791, 0, 1031428022, mod); return 1;
        case 1056440321: set_modulus_custom(q, 0x4, 0x10c52d4a, 1056440319, 0, 596300470, mod); return 1;
        case 1056178177: set_modulus_custom(q, 0x4, 0x11074e88, 1056178175, 0, 626569592, mod); return 1;
        case 1055260673: set_modulus_custom(q, 0x4, 0x11ef051e, 1055260671, 0, 287111906, mod); return 1;
        case 1054212097: set_modulus_custom(q, 0x4, 0x12f85437, 1054212095, 0, 890088393, mod); return 1;
        case 1054015489: set_modulus_custom(q, 0x4, 0x132a2218, 1054015487, 0, 51240424, mod); return 1;
        case 1053818881: set_modulus_custom(q, 0x4, 0x135bf4ba, 1053818879, 0, 159648582, mod); return 1;

        default:
            printf("Modulus const ratio values not found for ");
            print_zz("Modulus value", q);
            printf("Please try set_modulus_custom instead.");
            return 0;
    }
    return 0;
}
