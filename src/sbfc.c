/*
 * Succinct Bit-Cascaded Catalogs (SBFC) with sampling stride d
 * and checkpoint levels.  One source builds all three variants:
 *   -DVARIANT=0  materialized SBFC   (forward, checkpoint t = 1)
 *   -DVARIANT=1  M1-only SBFC        (forward, checkpoint t = k)
 *   -DVARIANT=2  reverse cascading   (select-based, no dup keys)
 *
 * usage: prog [--cpu c] [--csv] [--stride d] [--ckpt t]
 *             [--bv plain|ef|rrr|auto] [--pmu] [--verify]
 *             [--seed s] [--range r] [--vary] k n queries
 *
 * --ckpt t stores sampled keys at levels 1, 1+t, 1+2t, ...;
 * t = 0 stores none (zero key duplication).
 * --bv selects the origin-bit encoding: plain bits with a
 * two-level rank directory, Elias-Fano over the minority symbol,
 * RRR with 63-bit blocks, or the smallest of the three per level.
 * --pmu reads hardware counters (cycles, instructions, cache
 * references and misses, branches and branch misses) through
 * perf_event_open around the query loop only, and appends them to
 * the CSV line.  Pin the process with --cpu on hybrid CPUs.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#ifdef __BMI2__
#include <immintrin.h>
#endif

#ifndef VARIANT
#define VARIANT 0
#endif

enum { MAT = 0, M1ONLY = 1, REV = 2 };
enum { BV_PLAIN = 0, BV_EF = 1, BV_RRR = 2, BV_AUTO = 3 };

#define SEL_RATE 128
#define EF_SEL_RATE 256

typedef struct {
    uint64_t *w;
    uint32_t *l1;
    uint16_t *l2;
    uint32_t *sel;
    size_t m, nw, ones, nsel;
} bv_t;

/* bit string with sampled select1/select0 only (EF upper bits) */
typedef struct {
    uint64_t *w;
    uint32_t *s1, *s0;
    size_t nbits, nw, n1, n0;
} sbv_t;

/* Elias-Fano code of the positions of the minority symbol */
typedef struct {
    size_t m, u, ones;
    int l, comp;
    uint64_t *lo;
    size_t nlo;
    sbv_t h;
} ef_t;

/* RRR: 63-bit blocks, 6-bit class, enumerative offset */
#define RRR_B 63
#define RRR_SB 32
typedef struct {
    size_t m, nb, ones, nsb, offbits;
    uint64_t *cls, *off;
    uint32_t *sr, *sp;
} rrr_t;

typedef struct {
    int k, d;
    size_t *n, *m;
    uint32_t **L, **M;
    bv_t *B;
    ef_t *E;
    rrr_t *R;
    uint8_t *kind;
} sbfc_t;

/* navigation bits of every candidate encoding, summed */
typedef struct {
    double plain, plain_sel, ef, rrr15, rrr63, h0, ones, m;
} space_t;

static space_t space;

typedef struct {
    int cpu, csv, d, t, verify, vary, bv, pmu;
    uint64_t seed, range;
    int k;
    size_t n, q;
} opts_t;

static const char *variant_name[] = {
    "Materialized SBFC", "M1-Only SBFC", "Reverse-Cascade"
};

static void *xmalloc(size_t sz)
{
    void *p = malloc(sz ? sz : 1);
    if (p)
        return p;
    fprintf(stderr, "out of memory\n");
    exit(1);
}

static void *xcalloc(size_t cnt, size_t sz)
{
    void *p = calloc(cnt ? cnt : 1, sz);
    if (p)
        return p;
    fprintf(stderr, "out of memory\n");
    exit(1);
}

/* ---------------- bit vector with rank and select ------------- */

static void bv_init(bv_t *b, size_t m)
{
    memset(b, 0, sizeof *b);
    b->m = m;
    b->nw = m / 64 + 1;
    b->w = xcalloc(b->nw, sizeof *b->w);
}

static inline void bv_set(bv_t *b, size_t p)
{
    b->w[p >> 6] |= (uint64_t)1 << (p & 63);
}

static inline int bv_get(const bv_t *b, size_t p)
{
    return (int)((b->w[p >> 6] >> (p & 63)) & 1);
}

static void bv_build_select(bv_t *b)
{
    size_t cnt = 0;
    b->nsel = (b->ones + SEL_RATE - 1) / SEL_RATE;
    b->sel = xmalloc(b->nsel * sizeof *b->sel);
    for (size_t p = 0; p < b->m; p++) {
        if (!bv_get(b, p))
            continue;
        if (cnt % SEL_RATE == 0)
            b->sel[cnt / SEL_RATE] = (uint32_t)p;
        cnt++;
    }
}

static void bv_index(bv_t *b, int with_select)
{
    size_t tot = 0;
    b->l1 = xcalloc(b->nw / 32 + 1, sizeof *b->l1);
    b->l2 = xcalloc(b->nw, sizeof *b->l2);
    for (size_t i = 0; i < b->nw; i++) {
        if (i % 32 == 0)
            b->l1[i / 32] = (uint32_t)tot;
        b->l2[i] = (uint16_t)(tot - b->l1[i / 32]);
        tot += (size_t)__builtin_popcountll(b->w[i]);
    }
    b->ones = tot;
    if (with_select)
        bv_build_select(b);
}

/* number of ones in positions [0, p), for 0 <= p <= m */
static inline size_t rank1(const bv_t *b, size_t p)
{
    size_t w = p >> 6;
    uint64_t mask = ((uint64_t)1 << (p & 63)) - 1;
    return b->l1[w >> 5] + b->l2[w]
        + (size_t)__builtin_popcountll(b->w[w] & mask);
}

static size_t select_sb(const bv_t *b, size_t idx)
{
    size_t j = idx / SEL_RATE;
    size_t lo = b->sel[j] >> 11;
    size_t hi = j + 1 < b->nsel ? b->sel[j + 1] >> 11
                                : (b->nw - 1) >> 5;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (b->l1[mid] <= idx)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

/* position of the s-th one (s >= 1); -1 for s = 0, m if absent */
static int64_t select1(const bv_t *b, size_t s)
{
    if (s == 0)
        return -1;
    if (s > b->ones)
        return (int64_t)b->m;
    size_t idx = s - 1;
    size_t sb = select_sb(b, idx);
    size_t base = b->l1[sb];
    size_t wi = sb * 32;
    size_t wend = wi + 32 < b->nw ? wi + 32 : b->nw;
    while (wi + 1 < wend && base + b->l2[wi + 1] <= idx)
        wi++;
    size_t r = idx - base - b->l2[wi];
    uint64_t x = b->w[wi];
    while (r--)
        x &= x - 1;
    return (int64_t)(wi * 64 + (size_t)__builtin_ctzll(x));
}

static void bv_free(bv_t *b)
{
    free(b->w);
    free(b->l1);
    free(b->l2);
    free(b->sel);
    memset(b, 0, sizeof *b);
}

/* ---------------- select-only bit string ---------------------- */

static inline int sbv_get(const sbv_t *h, size_t q)
{
    return (int)((h->w[q >> 6] >> (q & 63)) & 1);
}

static inline size_t select_in_word(uint64_t x, size_t r)
{
#ifdef __BMI2__
    return (size_t)__builtin_ctzll(_pdep_u64((uint64_t)1 << r, x));
#else
    while (r--)
        x &= x - 1;
    return (size_t)__builtin_ctzll(x);
#endif
}

static void sbv_sample(sbv_t *h)
{
    size_t c1 = 0, c0 = 0;
    h->n1 = 0;
    for (size_t i = 0; i < h->nw; i++)
        h->n1 += (size_t)__builtin_popcountll(h->w[i]);
    h->n0 = h->nbits - h->n1;
    h->s1 = xmalloc((h->n1 / EF_SEL_RATE + 1) * sizeof *h->s1);
    h->s0 = xmalloc((h->n0 / EF_SEL_RATE + 1) * sizeof *h->s0);
    for (size_t q = 0; q < h->nbits; q++) {
        if (sbv_get(h, q)) {
            if (c1 % EF_SEL_RATE == 0)
                h->s1[c1 / EF_SEL_RATE] = (uint32_t)q;
            c1++;
            continue;
        }
        if (c0 % EF_SEL_RATE == 0)
            h->s0[c0 / EF_SEL_RATE] = (uint32_t)q;
        c0++;
    }
}

/* position of the idx-th (0-based) one, or zero if !one */
static size_t sbv_select(const sbv_t *h, size_t idx, int one)
{
    const uint32_t *smp = one ? h->s1 : h->s0;
    uint64_t flip = one ? 0 : ~(uint64_t)0;
    size_t j = idx / EF_SEL_RATE;
    size_t pos = smp[j];
    size_t r = idx - j * EF_SEL_RATE;
    size_t wi = pos >> 6;
    uint64_t x = (h->w[wi] ^ flip) & (~(uint64_t)0 << (pos & 63));
    for (;;) {
        size_t c = (size_t)__builtin_popcountll(x);
        if (r < c)
            break;
        r -= c;
        x = h->w[++wi] ^ flip;
    }
    return wi * 64 + select_in_word(x, r);
}

/* ---------------- Elias-Fano origin bits ---------------------- */

static inline uint64_t ef_low(const ef_t *e, size_t j)
{
    if (!e->l)
        return 0;
    size_t bit = j * (size_t)e->l;
    size_t wi = bit >> 6;
    unsigned off = (unsigned)(bit & 63);
    uint64_t v = e->lo[wi] >> off;
    if (off + (unsigned)e->l > 64)
        v |= e->lo[wi + 1] << (64 - off);
    return v & (((uint64_t)1 << e->l) - 1);
}

static void ef_put_low(ef_t *e, size_t j, uint64_t v)
{
    if (!e->l)
        return;
    size_t bit = j * (size_t)e->l;
    size_t wi = bit >> 6;
    unsigned off = (unsigned)(bit & 63);
    e->lo[wi] |= v << off;
    if (off + (unsigned)e->l > 64)
        e->lo[wi + 1] |= v >> (64 - off);
}

static int ef_width(size_t m, size_t u)
{
    int l = 0;
    if (u == 0)
        return 63;
    while (((size_t)2 << l) <= m / u)
        l++;
    return l;
}

static void ef_build(ef_t *e, const bv_t *b)
{
    memset(e, 0, sizeof *e);
    e->m = b->m;
    e->ones = b->ones;
    e->comp = b->ones > b->m / 2;
    e->u = e->comp ? b->m - b->ones : b->ones;
    e->l = ef_width(e->m, e->u);
    e->nlo = (e->u * (size_t)(e->u ? e->l : 0) + 63) / 64 + 1;
    e->lo = xcalloc(e->nlo, sizeof *e->lo);
    e->h.nbits = e->u + (e->m >> e->l) + 1;
    e->h.nw = e->h.nbits / 64 + 2;
    e->h.w = xcalloc(e->h.nw, sizeof *e->h.w);
    uint64_t mask = ((uint64_t)1 << e->l) - 1;
    size_t j = 0;
    for (size_t p = 0; p < e->m; p++) {
        if (bv_get(b, p) == e->comp)
            continue;
        size_t q = (p >> e->l) + j;
        e->h.w[q >> 6] |= (uint64_t)1 << (q & 63);
        ef_put_low(e, j++, p & mask);
    }
    sbv_sample(&e->h);
}

/* minority symbols before p; *is tells if p holds one */
static size_t ef_rank_minor(const ef_t *e, size_t p, int *is)
{
    size_t hp = p >> e->l;
    uint64_t lp = p & (((uint64_t)1 << e->l) - 1);
    size_t q = 0, j = 0;
    if (hp) {
        q = sbv_select(&e->h, hp - 1, 0) + 1;
        j = q - hp;
    }
    while (sbv_get(&e->h, q) && ef_low(e, j) < lp) {
        q++;
        j++;
    }
    *is = sbv_get(&e->h, q) && ef_low(e, j) == lp;
    return j;
}

static inline size_t ef_rank(const ef_t *e, size_t p, int *bit)
{
    int is;
    size_t r = ef_rank_minor(e, p, &is);
    if (bit)
        *bit = e->comp ? !is : is;
    return e->comp ? p - r : r;
}

static int64_t ef_select1(const ef_t *e, size_t s)
{
    if (s == 0)
        return -1;
    if (s > e->ones)
        return (int64_t)e->m;
    if (!e->comp) {
        size_t q = sbv_select(&e->h, s - 1, 1);
        return (int64_t)(((q - (s - 1)) << e->l) | ef_low(e, s - 1));
    }
    size_t lo = 0, hi = e->m - 1;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (ef_rank(e, mid + 1, NULL) >= s)
            hi = mid;
        else
            lo = mid + 1;
    }
    return (int64_t)lo;
}

static double ef_bits(const ef_t *e)
{
    double smp = (double)(e->h.n1 / EF_SEL_RATE + 1)
               + (double)(e->h.n0 / EF_SEL_RATE + 1);
    double lo = 64.0 * ceil((double)e->u * e->l / 64.0);
    if (!e->u)
        lo = 0;
    return lo + 64.0 * ceil((double)e->h.nbits / 64.0) + 32.0 * smp;
}

static void ef_free(ef_t *e)
{
    free(e->lo);
    free(e->h.w);
    free(e->h.s1);
    free(e->h.s0);
}

/* ---------------- space of the candidate encodings ------------ */

static uint64_t binom[64][64];

static void init_binom(void)
{
    for (int n = 0; n < 64; n++) {
        binom[n][0] = 1;
        for (int c = 1; c <= n; c++)
            binom[n][c] = binom[n - 1][c - 1]
                        + (c < n ? binom[n - 1][c] : 0);
    }
}

static int ceil_log2(uint64_t x)
{
    return x <= 1 ? 0 : 64 - __builtin_clzll(x - 1);
}

static uint64_t get_bits(const uint64_t *w, size_t pos, int len)
{
    size_t wi = pos >> 6;
    unsigned off = (unsigned)(pos & 63);
    uint64_t v = w[wi] >> off;
    if (off + (unsigned)len > 64)
        v |= w[wi + 1] << (64 - off);
    return v & (((uint64_t)1 << len) - 1);
}

/* RRR with b-bit blocks, (rank, pointer) sample per 32 blocks */
static double rrr_bits(const bv_t *b, int bs)
{
    double bits = 0;
    size_t nb = (b->m + (size_t)bs - 1) / (size_t)bs;
    int cls = ceil_log2((uint64_t)bs + 1);
    for (size_t j = 0; j < nb; j++) {
        size_t pos = j * (size_t)bs;
        int len = b->m - pos < (size_t)bs ? (int)(b->m - pos) : bs;
        int c = __builtin_popcountll(get_bits(b->w, pos, len));
        bits += cls + ceil_log2(binom[bs][c]);
    }
    return bits + 64.0 * (double)((nb + 31) / 32);
}

static void put_bits(uint64_t *w, size_t pos, int len, uint64_t v)
{
    if (!len)
        return;
    size_t wi = pos >> 6;
    unsigned off = (unsigned)(pos & 63);
    w[wi] |= v << off;
    if (off + (unsigned)len > 64)
        w[wi + 1] |= v >> (64 - off);
}

static int rrr_offw[RRR_B + 1];

static void init_rrr(void)
{
    for (int c = 0; c <= RRR_B; c++)
        rrr_offw[c] = ceil_log2(binom[RRR_B][c]);
}

static uint64_t rrr_encode(uint64_t v, int c)
{
    uint64_t off = 0;
    for (int i = RRR_B - 1; i >= 0 && c; i--) {
        if (!((v >> i) & 1))
            continue;
        off += binom[i][c];
        c--;
    }
    return off;
}

static uint64_t rrr_decode(uint64_t off, int c)
{
    uint64_t v = 0;
    for (int i = RRR_B - 1; i >= 0 && c; i--) {
        if (off < binom[i][c])
            continue;
        off -= binom[i][c];
        v |= (uint64_t)1 << i;
        c--;
    }
    return v;
}

static inline int rrr_cls(const rrr_t *r, size_t j)
{
    return (int)get_bits(r->cls, j * 6, 6);
}

static uint64_t rrr_block(const bv_t *b, size_t j)
{
    size_t pos = j * RRR_B;
    int len = b->m - pos < RRR_B ? (int)(b->m - pos) : RRR_B;
    return get_bits(b->w, pos, len);
}

static void rrr_build(rrr_t *r, const bv_t *b)
{
    memset(r, 0, sizeof *r);
    r->m = b->m;
    r->ones = b->ones;
    r->nb = (b->m + RRR_B - 1) / RRR_B;
    r->nsb = r->nb / RRR_SB + 1;
    for (size_t j = 0; j < r->nb; j++)
        r->offbits += (size_t)
            rrr_offw[__builtin_popcountll(rrr_block(b, j))];
    r->cls = xcalloc(r->nb * 6 / 64 + 2, sizeof *r->cls);
    r->off = xcalloc(r->offbits / 64 + 2, sizeof *r->off);
    r->sr = xmalloc(r->nsb * sizeof *r->sr);
    r->sp = xmalloc(r->nsb * sizeof *r->sp);
    size_t rank = 0, ptr = 0;
    for (size_t j = 0; j < r->nb; j++) {
        if (j % RRR_SB == 0) {
            r->sr[j / RRR_SB] = (uint32_t)rank;
            r->sp[j / RRR_SB] = (uint32_t)ptr;
        }
        uint64_t v = rrr_block(b, j);
        int c = __builtin_popcountll(v);
        put_bits(r->cls, j * 6, 6, (uint64_t)c);
        put_bits(r->off, ptr, rrr_offw[c], rrr_encode(v, c));
        rank += (size_t)c;
        ptr += (size_t)rrr_offw[c];
    }
    if (r->nb % RRR_SB == 0) {
        r->sr[r->nb / RRR_SB] = (uint32_t)rank;
        r->sp[r->nb / RRR_SB] = (uint32_t)ptr;
    }
}

/* ones before block j, and bit offset of its code */
static size_t rrr_seek(const rrr_t *r, size_t j, size_t *ptr)
{
    size_t sb = j / RRR_SB;
    size_t rank = r->sr[sb];
    *ptr = r->sp[sb];
    for (size_t t = sb * RRR_SB; t < j; t++) {
        int c = rrr_cls(r, t);
        rank += (size_t)c;
        *ptr += (size_t)rrr_offw[c];
    }
    return rank;
}

static size_t rrr_rank(const rrr_t *r, size_t p, int *bit)
{
    size_t j = p / RRR_B, ptr;
    int q = (int)(p % RRR_B);
    size_t rank = rrr_seek(r, j, &ptr);
    int c = j < r->nb ? rrr_cls(r, j) : 0;
    int bt = 0;
    if (c == RRR_B) {
        bt = 1;
        rank += (size_t)q;
    } else if (c) {
        uint64_t off = get_bits(r->off, ptr, rrr_offw[c]);
        for (int i = RRR_B - 1; i > q && c; i--) {
            if (off < binom[i][c])
                continue;
            off -= binom[i][c];
            c--;
        }
        bt = c && off >= binom[q][c];
        rank += (size_t)(c - bt);
    }
    if (bit)
        *bit = bt;
    return rank;
}

static int64_t rrr_select1(const rrr_t *r, size_t s)
{
    if (s == 0)
        return -1;
    if (s > r->ones)
        return (int64_t)r->m;
    size_t lo = 0, hi = (r->nb - 1) / RRR_SB;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (r->sr[mid] < s)
            lo = mid;
        else
            hi = mid - 1;
    }
    size_t rank = r->sr[lo], ptr = r->sp[lo], j = lo * RRR_SB;
    int c = rrr_cls(r, j);
    while (rank + (size_t)c < s) {
        rank += (size_t)c;
        ptr += (size_t)rrr_offw[c];
        c = rrr_cls(r, ++j);
    }
    uint64_t v = rrr_decode(get_bits(r->off, ptr, rrr_offw[c]), c);
    return (int64_t)(j * RRR_B + select_in_word(v, s - rank - 1));
}

static double rrr_struct_bits(const rrr_t *r)
{
    return 6.0 * (double)r->nb + (double)r->offbits
         + 64.0 * (double)((r->nb + RRR_SB - 1) / RRR_SB);
}

static void rrr_free(rrr_t *r)
{
    free(r->cls);
    free(r->off);
    free(r->sr);
    free(r->sp);
}

static double h0_bits(size_t m, size_t u)
{
    double v = lgamma((double)m + 1) - lgamma((double)u + 1)
             - lgamma((double)(m - u) + 1);
    return v / log(2.0);
}

static double plain_bits(const bv_t *b, int with_select)
{
    size_t w = (b->m + 63) / 64;
    size_t sb = (w + 31) / 32;
    double bits = 80.0 * (double)w + 32.0 * (double)sb;
    if (with_select)
        bits += 32.0 * (double)((b->ones + SEL_RATE - 1) / SEL_RATE);
    return bits;
}

static void account(const bv_t *b, const ef_t *e, const rrr_t *r)
{
    space.plain += plain_bits(b, 0);
    space.plain_sel += plain_bits(b, 1);
    space.ef += ef_bits(e);
    space.rrr15 += rrr_bits(b, 15);
    space.rrr63 += rrr_struct_bits(r);
    space.h0 += h0_bits(b->m, b->ones);
    space.ones += (double)b->ones;
    space.m += (double)b->m;
}

static int choose_kind(const bv_t *b, const ef_t *e,
                       const rrr_t *r, int mode, int with_select)
{
    if (mode != BV_AUTO)
        return mode;
    double pb = plain_bits(b, with_select);
    double eb = ef_bits(e), rb = rrr_struct_bits(r);
    if (eb < pb && eb <= rb)
        return BV_EF;
    return rb < pb ? BV_RRR : BV_PLAIN;
}

/* ---------------- construction -------------------------------- */

static uint32_t *merge_level(sbfc_t *s, int i, const uint32_t *below)
{
    int d = s->d;
    size_t ns = s->m[i + 1] / (size_t)d;
    size_t a = 0, j = 0;
    const uint32_t *L = s->L[i];
    s->m[i] = s->n[i] + ns;
    uint32_t *cur = xmalloc(s->m[i] * sizeof *cur);
    bv_init(&s->B[i], s->m[i]);
    for (size_t p = 0; p < s->m[i]; p++) {
        uint32_t smp = j < ns ? below[j * d + d - 1] : 0;
        if (a < s->n[i] && (j == ns || L[a] <= smp)) {
            cur[p] = L[a++];
            continue;
        }
        cur[p] = smp;
        j++;
        bv_set(&s->B[i], p);
    }
    return cur;
}

static int is_ckpt(int i, int k, int t)
{
    return t > 0 && i < k - 1 && i % t == 0;
}

static void encode_level(sbfc_t *s, int i, int mode,
                         int with_select)
{
    bv_t *b = &s->B[i];
    ef_t *e = &s->E[i];
    rrr_t *r = &s->R[i];
    bv_index(b, with_select);
    ef_build(e, b);
    rrr_build(r, b);
    account(b, e, r);
    int kd = choose_kind(b, e, r, mode, with_select);
    s->kind[i] = (uint8_t)kd;
    if (kd != BV_PLAIN)
        bv_free(b);
    if (kd != BV_EF)
        ef_free(e);
    if (kd != BV_RRR)
        rrr_free(r);
}

static void sbfc_build(sbfc_t *s, uint32_t **lists, size_t *n,
                       int k, int d, int t, int with_select,
                       int mode)
{
    s->k = k;
    s->d = d;
    s->L = lists;
    s->n = n;
    s->m = xcalloc((size_t)k, sizeof *s->m);
    s->M = xcalloc((size_t)k, sizeof *s->M);
    s->B = xcalloc((size_t)k, sizeof *s->B);
    s->E = xcalloc((size_t)k, sizeof *s->E);
    s->R = xcalloc((size_t)k, sizeof *s->R);
    s->kind = xcalloc((size_t)k, 1);
    s->m[k - 1] = n[k - 1];
    uint32_t *below = lists[k - 1];
    for (int i = k - 2; i >= 0; i--) {
        uint32_t *cur = merge_level(s, i, below);
        encode_level(s, i, mode, with_select);
        if (i + 1 < k - 1 && !s->M[i + 1])
            free(below);
        if (is_ckpt(i, k, t))
            s->M[i] = cur;
        below = cur;
    }
    if (k > 1 && !s->M[0])
        free(below);
}

static void sbfc_free(sbfc_t *s)
{
    for (int i = 0; i < s->k; i++) {
        free(s->M[i]);
        if (i == s->k - 1)
            continue;
        if (s->kind[i] == BV_EF)
            ef_free(&s->E[i]);
        else if (s->kind[i] == BV_RRR)
            rrr_free(&s->R[i]);
        else
            bv_free(&s->B[i]);
    }
    free(s->m);
    free(s->M);
    free(s->B);
    free(s->E);
    free(s->R);
    free(s->kind);
}

/* ---------------- queries ------------------------------------- */

static inline size_t nav_rank(const sbfc_t *s, int i, size_t p,
                              int *bit)
{
    if (s->kind[i] == BV_EF)
        return ef_rank(&s->E[i], p, bit);
    if (s->kind[i] == BV_RRR)
        return rrr_rank(&s->R[i], p, bit);
    const bv_t *b = &s->B[i];
    if (bit)
        *bit = bv_get(b, p);
    return rank1(b, p);
}

static inline int64_t nav_select(const sbfc_t *s, int i, size_t sm)
{
    if (s->kind[i] == BV_EF)
        return ef_select1(&s->E[i], sm);
    if (s->kind[i] == BV_RRR)
        return rrr_select1(&s->R[i], sm);
    return select1(&s->B[i], sm);
}

static size_t lb_arr(const uint32_t *a, size_t lo, size_t hi,
                     uint32_t x)
{
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (a[mid] < x)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* key of virtual entry M_i[p], resolved through the samples */
static uint32_t key(const sbfc_t *s, int i, size_t p)
{
    for (;;) {
        if (s->M[i])
            return s->M[i][p];
        if (i == s->k - 1)
            return s->L[i][p];
        int bit;
        size_t c = nav_rank(s, i, p, &bit);
        if (!bit)
            return s->L[i][p - c];
        p = (size_t)s->d * c + (size_t)s->d - 1;
        i++;
    }
}

static size_t lb_virtual(const sbfc_t *s, int i, size_t lo,
                         size_t hi, uint32_t x)
{
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (key(s, i, mid) < x)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static void query_forward(const sbfc_t *s, uint32_t x, size_t *out)
{
    int k = s->k;
    size_t d = (size_t)s->d;
    size_t p = s->M[0] ? lb_arr(s->M[0], 0, s->m[0], x)
                       : lb_virtual(s, 0, 0, s->m[0], x);
    for (int i = 0; i < k - 1; i++) {
        size_t c = nav_rank(s, i, p, NULL);
        out[i] = p - c;
        size_t lo = d * c;
        size_t hi = lo + d - 1;
        if (hi > s->m[i + 1])
            hi = s->m[i + 1];
        p = lb_virtual(s, i + 1, lo, hi, x);
    }
    out[k - 1] = p;
}

static void query_reverse(const sbfc_t *s, uint32_t x, size_t *out)
{
    int k = s->k;
    size_t d = (size_t)s->d;
    size_t p = lb_arr(s->L[k - 1], 0, s->n[k - 1], x);
    out[k - 1] = p;
    for (int i = k - 1; i > 0; i--) {
        size_t sm = p / d;
        int64_t a = nav_select(s, i - 1, sm);
        int64_t z = nav_select(s, i - 1, sm + 1);
        size_t lo = (size_t)(a + 1 - (int64_t)sm);
        size_t hi = (size_t)(z - (int64_t)sm);
        out[i - 1] = lb_arr(s->L[i - 1], lo, hi, x);
        p = out[i - 1] + sm;
    }
}

static void query(const sbfc_t *s, uint32_t x, size_t *out)
{
    if (VARIANT == REV)
        query_reverse(s, x, out);
    else
        query_forward(s, x, out);
}

/* ---------------- space accounting ---------------------------- */

static double extra_key_bits(const sbfc_t *s)
{
    double bits = 0;
    for (int i = 0; i < s->k; i++)
        if (s->M[i])
            bits += 32.0 * (double)(s->m[i] - s->n[i]);
    return bits;
}

static double struct_bits(const sbfc_t *s)
{
    double bits = 0;
    for (int i = 0; i < s->k - 1; i++) {
        if (s->kind[i] == BV_EF) {
            bits += ef_bits(&s->E[i]);
            continue;
        }
        if (s->kind[i] == BV_RRR) {
            bits += rrr_struct_bits(&s->R[i]);
            continue;
        }
        bits += plain_bits(&s->B[i], s->B[i].nsel > 0);
    }
    return bits;
}

/* ---------------- input generation ---------------------------- */

static uint64_t rng_state = 88172645463325252ULL;

static uint64_t rnd(void)
{
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 2685821657736338717ULL;
}

static uint32_t rnd_val(uint64_t range)
{
    if (range)
        return (uint32_t)(rnd() % range);
    return (uint32_t)(rnd() >> 32);
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static uint32_t **make_lists(const opts_t *o, size_t *n)
{
    uint32_t **L = xmalloc((size_t)o->k * sizeof *L);
    for (int i = 0; i < o->k; i++) {
        n[i] = o->vary ? (size_t)(rnd() % (o->n + 1)) : o->n;
        L[i] = xmalloc(n[i] * sizeof *L[i]);
        for (size_t j = 0; j < n[i]; j++)
            L[i][j] = rnd_val(o->range);
        qsort(L[i], n[i], sizeof *L[i], cmp_u32);
    }
    return L;
}

/* ---------------- CPU pinning and argument parsing ------------ */

static int pin_to_cpu(int cpu_id)
{
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs <= 0 || cpu_id < 0 || cpu_id >= nprocs) {
        fprintf(stderr, "Error: CPU core %d out of range [0, %ld)\n",
                cpu_id, nprocs > 0 ? nprocs : 0);
        return -1;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu_id, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity");
        return -1;
    }
    return 0;
}

static int parse_ull(const char *arg, unsigned long long *out)
{
    if (!arg || *arg == '\0' || *arg == '-')
        return -1;
    errno = 0;
    char *end = NULL;
    unsigned long long v = strtoull(arg, &end, 10);
    if (errno != 0 || *end != '\0')
        return -1;
    *out = v;
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [--cpu c] [--csv] [--stride d] [--ckpt t]\n"
            "       [--bv plain|ef|rrr|auto] [--pmu] [--verify]\n"
            "       [--seed s] [--range r] [--vary] k n queries\n",
            prog);
    exit(2);
}

static int parse_bv(opts_t *o, const char *v)
{
    static const char *names[] = { "plain", "ef", "rrr", "auto" };
    for (int j = 0; j < 4; j++)
        if (v && !strcmp(v, names[j]))
            return o->bv = j, 0;
    return -1;
}

static int parse_opt(opts_t *o, int argc, char **argv, int *i)
{
    const char *a = argv[*i];
    unsigned long long v = 0;
    if (!strcmp(a, "--bv"))
        return *i + 1 < argc ? parse_bv(o, argv[++*i]) : -1;
    if (!strcmp(a, "--csv"))
        return o->csv = 1, 0;
    if (!strcmp(a, "--pmu"))
        return o->pmu = 1, 0;
    if (!strcmp(a, "--verify"))
        return o->verify = 1, 0;
    if (!strcmp(a, "--vary"))
        return o->vary = 1, 0;
    if (*i + 1 >= argc || parse_ull(argv[*i + 1], &v) != 0)
        return -1;
    (*i)++;
    if (!strcmp(a, "--cpu") && v <= INT_MAX)
        return o->cpu = (int)v, 0;
    if (!strcmp(a, "--stride") && v >= 2 && v <= 1024)
        return o->d = (int)v, 0;
    if (!strcmp(a, "--ckpt") && v <= INT_MAX)
        return o->t = (int)v, 0;
    if (!strcmp(a, "--seed"))
        return o->seed = v, 0;
    if (!strcmp(a, "--range") && v <= (1ULL << 32))
        return o->range = v, 0;
    return -1;
}

static void parse_args(opts_t *o, int argc, char **argv)
{
    unsigned long long pos[3];
    int np = 0;
    *o = (opts_t){ .cpu = -1, .d = 2, .t = -1, .seed = 1 };
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--", 2)) {
            if (parse_opt(o, argc, argv, &i) != 0)
                usage(argv[0]);
            continue;
        }
        if (np == 3 || parse_ull(argv[i], &pos[np++]) != 0)
            usage(argv[0]);
    }
    if (np != 3 || pos[0] < 1 || pos[0] > 4096)
        usage(argv[0]);
    o->k = (int)pos[0];
    o->n = (size_t)pos[1];
    o->q = (size_t)pos[2];
    if (o->t < 0)
        o->t = VARIANT == M1ONLY ? o->k : 1;
    if (VARIANT == REV)
        o->t = 0;
}

/* ---------------- driver -------------------------------------- */

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int verify(const sbfc_t *s, const uint32_t *qs, size_t nq,
                  size_t *out)
{
    for (size_t j = 0; j < nq; j++) {
        query(s, qs[j], out);
        for (int i = 0; i < s->k; i++) {
            size_t e = lb_arr(s->L[i], 0, s->n[i], qs[j]);
            if (out[i] == e)
                continue;
            fprintf(stderr, "MISMATCH x=%u list=%d got=%zu "
                    "want=%zu\n", qs[j], i + 1, out[i], e);
            return -1;
        }
    }
    return 0;
}

/* ---------------- hardware counters --------------------------- */

#define NPMU 6

static const uint64_t pmu_cfg[NPMU] = {
    PERF_COUNT_HW_CPU_CYCLES, PERF_COUNT_HW_INSTRUCTIONS,
    PERF_COUNT_HW_CACHE_REFERENCES, PERF_COUNT_HW_CACHE_MISSES,
    PERF_COUNT_HW_BRANCH_INSTRUCTIONS, PERF_COUNT_HW_BRANCH_MISSES
};

typedef struct {
    int on, fd[NPMU];
    double val[NPMU];
} pmu_t;

static pmu_t pmu;

static int cpu_in_list(const char *path, int cpu)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    int a, b, hit = 0;
    char sep;
    while (!hit && fscanf(f, "%d", &a) == 1) {
        b = a;
        if (fscanf(f, "%c", &sep) == 1 && sep == '-') {
            if (fscanf(f, "%d", &b) != 1)
                break;
            if (fscanf(f, "%c", &sep) != 1)
                sep = 0;
        }
        hit = cpu >= a && cpu <= b;
    }
    fclose(f);
    return hit;
}

/* PMU type owning this CPU on hybrid parts; 0 = generic */
static uint64_t pmu_type(int cpu)
{
    static const char *names[] = { "cpu_core", "cpu_atom" };
    char path[128];
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path,
                 "/sys/bus/event_source/devices/%s/cpus", names[i]);
        if (!cpu_in_list(path, cpu))
            continue;
        snprintf(path, sizeof path,
                 "/sys/bus/event_source/devices/%s/type", names[i]);
        FILE *f = fopen(path, "r");
        unsigned long long t = 0;
        if (f && fscanf(f, "%llu", &t) != 1)
            t = 0;
        if (f)
            fclose(f);
        return t;
    }
    return 0;
}

static int pmu_open(int pinned)
{
    int cpu = sched_getcpu();
    uint64_t type = pmu_type(cpu);
    if (!pinned && type)
        fprintf(stderr, "warning: --pmu without --cpu on a hybrid "
                "CPU counts only one core type\n");
    for (int i = 0; i < NPMU; i++) {
        struct perf_event_attr a;
        memset(&a, 0, sizeof a);
        a.size = sizeof a;
        a.type = PERF_TYPE_HARDWARE;
        a.config = pmu_cfg[i] | (type << 32);
        a.disabled = i == 0;
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        a.read_format = PERF_FORMAT_GROUP
                      | PERF_FORMAT_TOTAL_TIME_ENABLED
                      | PERF_FORMAT_TOTAL_TIME_RUNNING;
        int group = i ? pmu.fd[0] : -1;
        pmu.fd[i] = (int)syscall(SYS_perf_event_open, &a, 0, -1,
                                 group, 0);
        if (pmu.fd[i] < 0) {
            perror("perf_event_open (check "
                   "/proc/sys/kernel/perf_event_paranoid)");
            return -1;
        }
    }
    pmu.on = 1;
    return 0;
}

static void pmu_start(void)
{
    if (!pmu.on)
        return;
    ioctl(pmu.fd[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
    ioctl(pmu.fd[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
}

static void pmu_stop(void)
{
    struct {
        uint64_t nr, enabled, running, v[NPMU];
    } r;
    if (!pmu.on)
        return;
    ioctl(pmu.fd[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    if (read(pmu.fd[0], &r, sizeof r) != (ssize_t)sizeof r) {
        perror("read counters");
        return;
    }
    double scale = r.running ? (double)r.enabled / (double)r.running
                             : 0.0;
    for (int i = 0; i < NPMU; i++)
        pmu.val[i] = (double)r.v[i] * scale;
}

static void pmu_close(void)
{
    for (int i = 0; pmu.on && i < NPMU; i++)
        close(pmu.fd[i]);
}

static double bench(const sbfc_t *s, const uint32_t *qs, size_t nq,
                    size_t *out, uint64_t *sum)
{
    pmu_start();
    double t0 = now_ns();
    for (size_t j = 0; j < nq; j++) {
        query(s, qs[j], out);
        *sum += out[0] + out[s->k - 1];
    }
    double t1 = now_ns();
    pmu_stop();
    return nq ? (t1 - t0) / (double)nq : 0.0;
}

static void report_pmu(const opts_t *o)
{
    double q = o->q ? (double)o->q : 1.0;
    double *v = pmu.val;
    printf("cycles / query   : %.1f\n", v[0] / q);
    printf("instr. / query   : %.1f\n", v[1] / q);
    printf("IPC              : %.3f\n", v[0] ? v[1] / v[0] : 0.0);
    printf("cache miss rate  : %.2f%%\n",
           v[2] ? 100.0 * v[3] / v[2] : 0.0);
    printf("cache miss/query : %.2f\n", v[3] / q);
    printf("branch miss rate : %.2f%%\n",
           v[4] ? 100.0 * v[5] / v[4] : 0.0);
}

static void report(const opts_t *o, const sbfc_t *s, size_t N,
                   double ns, uint64_t sum)
{
    double div = N ? (double)N : 1.0;
    double ek = extra_key_bits(s) / div;
    double st = struct_bits(s) / div;
    if (o->csv) {
        printf("%d,%zu,%.4f,%.4f,%.4f,%.2f",
               o->k, N, ek, st, ek + st, ns);
        for (int i = 0; pmu.on && i < NPMU; i++)
            printf(",%.0f", pmu.val[i]);
        printf("\n");
        return;
    }
    printf("variant          : %s\n", variant_name[VARIANT]);
    printf("k, N, stride, t  : %d, %zu, %d, %d\n",
           o->k, N, o->d, o->t);
    printf("extra key bits/N : %.4f\n", ek);
    printf("struct bits/N    : %.4f\n", st);
    printf("total extra/N    : %.4f\n", ek + st);
    printf("ns per query     : %.2f\n", ns);
    printf("checksum         : %llu\n", (unsigned long long)sum);
    if (pmu.on)
        report_pmu(o);
    printf("density of ones  : %.4f\n",
           space.m ? space.ones / space.m : 0.0);
    printf("nav bits/N by encoding (all levels):\n");
    printf("  plain rank     : %.4f\n", space.plain / div);
    printf("  plain + select : %.4f\n", space.plain_sel / div);
    printf("  Elias-Fano     : %.4f\n", space.ef / div);
    printf("  RRR b=15       : %.4f\n", space.rrr15 / div);
    printf("  RRR b=63       : %.4f\n", space.rrr63 / div);
    printf("  log2 C(m,u)    : %.4f\n", space.h0 / div);
}

int main(int argc, char **argv)
{
    opts_t o;
    parse_args(&o, argc, argv);
    if (o.cpu >= 0 && pin_to_cpu(o.cpu) != 0)
        return 1;
    rng_state ^= o.seed * 0x9E3779B97F4A7C15ULL;
    if (!rng_state)
        rng_state = 1;

    size_t *n = xmalloc((size_t)o.k * sizeof *n);
    uint32_t **L = make_lists(&o, n);
    size_t N = 0;
    for (int i = 0; i < o.k; i++)
        N += n[i];
    uint32_t *qs = xmalloc(o.q * sizeof *qs);
    for (size_t j = 0; j < o.q; j++)
        qs[j] = o.range ? rnd_val(o.range + 2) : rnd_val(0);

    sbfc_t s;
    init_binom();
    init_rrr();
    sbfc_build(&s, L, n, o.k, o.d, o.t, VARIANT == REV, o.bv);
    size_t *out = xmalloc((size_t)o.k * sizeof *out);
    uint64_t sum = 0;
    if (o.pmu && pmu_open(o.cpu >= 0) != 0)
        return 1;
    double ns = bench(&s, qs, o.q, out, &sum);
    int rc = o.verify ? verify(&s, qs, o.q, out) : 0;
    if (rc == 0)
        report(&o, &s, N, ns, sum);

    pmu_close();
    sbfc_free(&s);
    for (int i = 0; i < o.k; i++)
        free(L[i]);
    free(L);
    free(n);
    free(qs);
    free(out);
    return rc ? 1 : 0;
}
