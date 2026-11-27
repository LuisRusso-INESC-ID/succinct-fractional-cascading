/* Unit test: rank/select of plain, Elias-Fano and RRR bit strings
 * against a naive model, on random strings of every density and
 * with long runs of ones and zeros. */
#define main sbfc_main
#include "sbfc.c"
#undef main

static void fill(bv_t *b, size_t m, int mode)
{
    bv_init(b, m);
    uint64_t dens = rnd() % 1001;
    for (size_t p = 0; p < m; p++) {
        int bit = rnd() % 1000 < dens;
        if (mode == 1)
            bit = (p / 200) % 2;
        if (mode == 2)
            bit = p % 97 != 0;
        if (bit)
            bv_set(b, p);
    }
}

static int check(size_t m, int mode)
{
    bv_t b;
    ef_t e;
    rrr_t r;
    fill(&b, m, mode);
    bv_index(&b, 1);
    ef_build(&e, &b);
    rrr_build(&r, &b);
    size_t ones = 0, bad = 0;
    for (size_t p = 0; p <= m; p++) {
        int bit = p < m ? bv_get(&b, p) : 0, be = 0, br = 0;
        size_t re = ef_rank(&e, p, &be), rr = rrr_rank(&r, p, &br);
        bad += rank1(&b, p) != ones || re != ones || rr != ones;
        bad += p < m && (be != bit || br != bit);
        if (bit) {
            ones++;
            int64_t q = (int64_t)p;
            bad += select1(&b, ones) != q;
            bad += ef_select1(&e, ones) != q;
            bad += rrr_select1(&r, ones) != q;
        }
    }
    bad += ef_select1(&e, ones + 1) != (int64_t)m;
    bad += rrr_select1(&r, ones + 1) != (int64_t)m;
    bad += ef_select1(&e, 0) != -1 || rrr_select1(&r, 0) != -1;
    bv_free(&b);
    ef_free(&e);
    rrr_free(&r);
    if (bad)
        fprintf(stderr, "FAIL m=%zu mode=%d (%zu errors)\n",
                m, mode, bad);
    return bad != 0;
}

int main(void)
{
    init_binom();
    init_rrr();
    int fails = 0;
    for (int t = 0; t < 3000; t++) {
        size_t m = rnd() % (t < 2000 ? 300 : 20000);
        fails += check(m, t % 5 < 3 ? 0 : t % 5 - 2);
    }
    if (fails)
        return 1;
    printf("OK  : bit-string rank/select (plain, EF, RRR-63)\n");
    return 0;
}
