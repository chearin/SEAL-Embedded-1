/* Exact integer tests. No CKKS approximation and no timing measurements. */
#include "ntt.h"
#include "intt.h"
#include "fft.h"
#include "uintmodarith.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SE_NTT_FAST
extern void ntt_non_lazy_inpl(const Parms *, const ZZ *, ZZ *);
#endif
extern void ntt_non_lazy_inpl_test(const Parms *, const ZZ *, ZZ *);
extern void ntt_non_lazy_inpl_test_v1(const Parms *, const ZZ *, ZZ *);
extern void cr_signed_ntt_lazy_inpl(const Parms *, const ZZ *, ZZ *);
static unsigned failures;
static size_t selected_n;
static uint32_t state = 0x12345678;
static uint32_t rng(void) { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
static uint32_t mul(uint32_t a, uint32_t b, uint32_t q) { return (uint64_t)a*b%q; }
static uint32_t power(uint32_t a, uint32_t e, uint32_t q)
{
    uint32_t r=1;
    for (; e; e>>=1, a=mul(a,a,q)) if (e&1) r=mul(r,a,q);
    return r;
}
static uint32_t canonical(int64_t x, uint32_t q) { x %= q; return x < 0 ? x+q : x; }
static void check(int ok, const char *what, size_t n, uint32_t q)
{
    if (!ok) { if (failures < 20) fprintf(stderr,"FAIL %s n=%zu q=%u\n",what,n,q); ++failures; }
}
static void equal(const ZZ *a, const ZZ *b, size_t n, ZZ q, const char *what)
{
    for(size_t i=0;i<n;++i) if(a[i]!=b[i]) { check(0,what,n,q); return; }
}
static void arithmetic(void)
{
    const ZZ primes[]={134176769,134111233,134012929,1062535169,1062469633,
      1061093377,1060765697,1060700161,1060175873,1058209793,1056440321,
      1056178177,1055260673,1054212097,1054015489,1053818881};
    unsigned before=failures;
    for(size_t p=0;p<sizeof(primes)/sizeof(*primes);++p)
    {
        Modulus m; if(!set_modulus(primes[p],&m)) abort(); ZZ q=m.value;
        ZZ edge[]={0,1,q-1,q/2};
        check((uint32_t)((uint64_t)q*m.inv_q)==UINT32_MAX,"inv_q",0,q);
        uint64_t ratio=UINT64_MAX/q; /* floor(2^64/q), q does not divide 2^64 */
        check((((uint64_t)m.const_ratio[1]<<32)|m.const_ratio[0])==ratio,"ratio",0,q);
        ZZ R=((uint64_t)1<<32)%q;
        check(m.R2==mul(R,R,q),"R2",0,q);
        if(q < 0x20000000) check(m.R==R,"R (signed NTT)",0,q);
        else printf("NOTE q=%u stored R=%u computed R=%u (30-bit NTT uses Barrett)\n",q,m.R,R);
        for(size_t i=0;i<10016;++i)
        {
            ZZ a=i<16?edge[i/4]:rng()%q, b=i<16?edge[i%4]:rng()%q;
            check(cr_barrett_mul(a,b,&m)==mul(a,b,q),"Barrett C",0,q);
            check(cr_barrett_mul_asm(a,b,&m)==mul(a,b,q),"Barrett asm",0,q);
            int32_t sa=(int32_t)a, sb=(int32_t)b;
            if (q < 0x20000000 && i >= 16) sa = rng() % (15*q);
            if(i&1) sa=-sa;
            if(i&2) sb=-sb;
            int64_t z=(int64_t)sa*sb;
            int64_t k=(uint32_t)((uint64_t)z*m.inv_q);
            if(k>=INT64_C(2147483648)) k-=INT64_C(4294967296);
            int64_t expected=(z+k*q)/INT64_C(4294967296);
            int32_t got=cr_mont_mul_asm(sa,sb,&m);
            check(got==expected,"signed Montgomery exact",0,q);
            check(cr_mont_mul(sa,sb,&m)==expected,"signed Montgomery C",0,q);
            check(mul(canonical(got,q),R,q)==canonical(z,q),"Montgomery congruence",0,q);
            ZZ um=cr_mont_mul_unsigned(a,b,&m);
            check(um<q && mul(um,R,q)==mul(a,b,q),"unsigned Montgomery",0,q);
        }
    }
    printf("Arithmetic: %s (%u failures)\n",failures==before?"PASS":"FAIL",failures-before);
}
static void reference_ntt(ZZ *v, const ZZ *roots, size_t n, ZZ q)
{
    for(size_t h=1,tt=n/2;h<n;h*=2,tt/=2)
        for(size_t j=0;j<h;++j) for(size_t k=j*2*tt;k<j*2*tt+tt;++k)
        {
            ZZ u=v[k],w=mul(v[k+tt],roots[h+j],q);
            v[k]=((uint64_t)u+w)%q; v[k+tt]=((uint64_t)u+q-w)%q;
        }
}
static void transform(size_t n, ZZ q, ZZ root)
{
    if (selected_n && n != selected_n) return;
    unsigned before=failures; Modulus m; if(!set_modulus(q,&m)) abort();
    Parms p={0};p.coeff_count=n;p.logn=get_log2(n);p.curr_modulus=&m;
    ZZ *r=calloc(n,sizeof(ZZ)), *ir=calloc(n,sizeof(ZZ));
    ZZ *a=calloc(n,sizeof(ZZ)),*b=calloc(n,sizeof(ZZ)),*v=calloc(n,sizeof(ZZ));
    ZZ *ref=calloc(n,sizeof(ZZ)),*base=calloc(n,sizeof(ZZ)),*sb=calloc(n,sizeof(ZZ));
    ZZ *rt=calloc(2*n,sizeof(ZZ)),*it=calloc(2*n,sizeof(ZZ));
    if(!r||!ir||!a||!b||!v||!ref||!base||!sb||!rt||!it) abort();
    check(power(root,n,q)==q-1 && power(root,2*n,q)==1,"primitive root",n,q);
    ZZ inv=power(root,q-2,q);
    for(size_t i=0;i<n;++i) r[bitrev(i,p.logn)]=power(root,i,q);
    ir[0]=1;
    for(size_t i=1;i<n;++i) ir[bitrev(i-1,p.logn)+1]=power(inv,i,q);
#if defined(SE_NTT_ONE_SHOT)
    ntt_roots_initialize(&p,rt); equal(r,rt,n,q,"production root generation");
#elif defined(SE_NTT_REG)
    memcpy(rt,r,n*sizeof(ZZ)); /* Loader not tested: independently generated regular roots. */
#elif defined(SE_NTT_FAST)
    for(size_t i=0;i<n;++i) { ((MUMO*)rt)[i].operand=r[i]; ((MUMO*)rt)[i].quotient=((uint64_t)r[i]<<32)/q; }
#endif
#if defined(SE_INTT_ONE_SHOT)
    intt_roots_initialize(&p,it); equal(ir,it,n,q,"production inverse root generation");
#elif defined(SE_INTT_REG)
    memcpy(it,ir,n*sizeof(ZZ));
#elif defined(SE_INTT_FAST)
    for(size_t i=0;i<n;++i) { ((MUMO*)it)[i].operand=ir[i]; ((MUMO*)it)[i].quotient=((uint64_t)ir[i]<<32)/q; }
#endif
    for(unsigned c=0;c<9;++c)
    {
        for(size_t i=0;i<n;++i) a[i]=c==0?0:c==1?1:c==2?q-1:c==3?(i==0):c==4?(i==1):c==5?(i%2?q-1:0):rng()%q;
        memcpy(ref,a,n*sizeof(ZZ));reference_ntt(ref,r,n,q);
        memcpy(v,a,n*sizeof(ZZ)); ntt_inpl(&p,rt,v);equal(ref,v,n,q,"public dispatch vs independent reference");
#ifndef SE_NTT_FAST
        memcpy(base,a,n*sizeof(ZZ)); ntt_non_lazy_inpl(&p,rt,base);equal(ref,base,n,q,"baseline vs independent reference");
        equal(base,v,n,q,"baseline vs public dispatch");
        memcpy(base,a,n*sizeof(ZZ)); cr_signed_ntt_lazy_inpl(&p,rt,base);
        equal(ref,base,n,q,"signed optimized vs independent reference");
        intt_inpl(&p,it,base); equal(a,base,n,q,"signed optimized round trip");
#endif
        intt_inpl(&p,it,v);equal(a,v,n,q,"NTT/INTT round trip");
    }
    /* Full dense schoolbook product in Z_q[x]/(x^n+1), independent of production mul_mod. */
    for(size_t i=0;i<n;++i) {a[i]=rng()%q;b[i]=rng()%q;}
    for(size_t i=0;i<n;++i) for(size_t j=0;j<n;++j)
    {
        size_t k=i+j; ZZ term=mul(a[i],b[j],q);
        if(k<n) sb[k]=((uint64_t)sb[k]+term)%q;
        else {k-=n;sb[k]=((uint64_t)sb[k]+q-term)%q;}
    }
    memcpy(ref,b,n*sizeof(ZZ));
    memcpy(v,a,n*sizeof(ZZ));ntt_inpl(&p,rt,v);ntt_inpl(&p,rt,b);
    for(size_t i=0;i<n;++i) v[i]=mul(v[i],b[i],q);
    intt_inpl(&p,it,v);equal(sb,v,n,q,"negacyclic schoolbook product");
#ifndef SE_NTT_FAST
    memcpy(v,a,n*sizeof(ZZ)); memcpy(b,ref,n*sizeof(ZZ));
    cr_signed_ntt_lazy_inpl(&p,rt,v); cr_signed_ntt_lazy_inpl(&p,rt,b);
    for(size_t i=0;i<n;++i) v[i]=mul(v[i],b[i],q);
    intt_inpl(&p,it,v);equal(sb,v,n,q,"signed optimized schoolbook product");
#endif
#if defined(SE_NTT_ONE_SHOT)
    /* Both legacy entry points also receive exactly n allocated input elements. */
    memcpy(v,a,n*sizeof(ZZ));reference_ntt(v,r,n,q);
    memcpy(base,a,n*sizeof(ZZ));ntt_non_lazy_inpl_test(&p,rt,base);equal(v,base,n,q,"legacy signed test");
    memcpy(base,a,n*sizeof(ZZ));ntt_non_lazy_inpl_test_v1(&p,rt,base);equal(v,base,n,q,"legacy signed v1");
#endif
    printf("NTT type=%d INTT type=%d n=%zu q=%u: %s (%u failures)\n",SE_NTT_TYPE,SE_INTT_TYPE,n,q,failures==before?"PASS":"FAIL",failures-before);
    free(r);free(ir);free(a);free(b);free(v);free(ref);free(base);free(sb);free(rt);free(it);
}
int main(int argc, char **argv)
{
    if (argc == 2) selected_n = strtoul(argv[1], NULL, 10);
    arithmetic();
    transform(1024,134012929,142143);transform(2048,134012929,85250);
    transform(4096,134012929,7470);transform(4096,134111233,3856);transform(4096,134176769,24149);
    transform(4096,1053818881,503422);transform(4096,1054015489,16768);transform(4096,1054212097,7305);
    printf("TOTAL failures=%u\n",failures);return failures?1:0;
}
