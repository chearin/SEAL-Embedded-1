/* Test/measurement setup only. Never call this inside a timed interval. */
#pragma once
#include "defines.h"

static inline void ckks_restore_public_key(size_t n, const ZZ *saved_pk,
                                           ZZ *pk_c0, ZZ *pk_c1)
{
    memcpy(pk_c0, saved_pk, n * sizeof(ZZ));
    memcpy(pk_c1, saved_pk + n, n * sizeof(ZZ));
}
