"""Reference model of SBFC used to check the paper's claims.

Run `python3 sbfc_check.py` for randomized tests, or
`python3 sbfc_check.py example` to print the running example,
or `python3 sbfc_check.py gaps` for the reverse-cascading gaps.
"""
import bisect
import math
import random
import sys

INF = float("inf")


def build(lists, d=2):
    k = len(lists)
    M = [None] * k
    B = [None] * k
    M[k - 1] = list(lists[k - 1])
    B[k - 1] = [0] * len(lists[k - 1])
    for i in range(k - 2, -1, -1):
        samp = M[i + 1][d - 1::d]
        merged = [(v, 0) for v in lists[i]] + [(v, 1) for v in samp]
        merged.sort(key=lambda t: (t[0], t[1]))
        M[i] = [v for v, _ in merged]
        B[i] = [b for _, b in merged]
    return M, B


def rank1(b, p):
    return sum(b[:p])


def select1(b, s):
    """Position of the s-th one (1-based); -1 if s=0, len if none."""
    if s == 0:
        return -1
    cnt = 0
    for j, bit in enumerate(b):
        cnt += bit
        if bit and cnt == s:
            return j
    return len(b)


def forward(lists, M, B, x, d=2):
    k = len(lists)
    p = bisect.bisect_left(M[0], x)
    out, trace = [], []
    for i in range(k):
        c = rank1(B[i], p)
        out.append(p - c)
        trace.append((p, c))
        if i == k - 1:
            break
        lo = d * c
        hi = min(d * c + d - 1, len(M[i + 1]))
        while lo < hi and M[i + 1][lo] < x:
            lo += 1
        p = lo
    return out, trace


def key(lists, B, i, p, ckpt, M, steps, d=2):
    while True:
        steps[0] += 1
        if i in ckpt:
            return M[i][p]
        c = rank1(B[i], p)
        if B[i][p] == 0:
            return lists[i][p - c]
        i, p = i + 1, d * c + d - 1


def chain(lists, B, i, p):
    """Sequence of (level, position) visited by key resolution."""
    path = [(i, p)]
    while B[i][p] == 1:
        c = rank1(B[i], p)
        i, p = i + 1, 2 * c + 1
        path.append((i, p))
    return path


def checkpointed(lists, M, B, x, ckpt, d=2):
    k = len(lists)
    steps = [0]
    p = bisect.bisect_left(M[0], x)
    out = []
    for i in range(k):
        c = rank1(B[i], p)
        out.append(p - c)
        if i == k - 1:
            break
        lo = d * c
        hi = min(d * c + d - 1, len(M[i + 1]))
        while lo < hi:
            mid = (lo + hi) // 2
            v = key(lists, B, i + 1, mid, ckpt, M, steps, d)
            if v < x:
                lo = mid + 1
            else:
                hi = mid
        p = lo
    return out, steps[0]


def reverse(lists, B, x, d=2):
    k = len(lists)
    p = bisect.bisect_left(lists[k - 1], x)
    out = [None] * k
    out[k - 1] = p
    trace = []
    for i in range(k - 1, 0, -1):
        s = p // d
        b = B[i - 1]
        lo = select1(b, s) + 1 - s
        hi = select1(b, s + 1) - s
        L = lists[i - 1]
        ell = bisect.bisect_left(L, x, lo, hi)
        trace.append((i, p, s, lo, hi, ell))
        out[i - 1] = ell
        p = ell + s
    return out, trace


def truth(lists, x):
    return [bisect.bisect_left(L, x) for L in lists]


def test(trials=3000):
    rnd = random.Random(1)
    for _ in range(trials):
        k = rnd.randint(1, 7)
        lists = [sorted(rnd.choices(range(60), k=rnd.randint(0, 12)))
                 for _ in range(k)]
        for d in (2, 3, 4):
            M, B = build(lists, d)
            for x in range(-1, 62):
                exp = truth(lists, x)
                assert forward(lists, M, B, x, d)[0] == exp
                assert reverse(lists, B, x, d)[0] == exp
                assert all(bisect.bisect_left(M[i], x)
                           == forward(lists, M, B, x, d)[1][i][0]
                           for i in range(k))
        for d in (2, 3, 4):
            M, B = build(lists, d)
            for ck in ({0}, set(range(0, k, 2)), set(range(k))):
                for x in range(-1, 62):
                    got = checkpointed(lists, M, B, x, ck, d)[0]
                    assert got == truth(lists, x)
    print("all randomized checks passed")


def adversary(k, n):
    big = 10 ** 6
    lists = [[big + i * n + j for j in range(n)]
             for i in range(k - 1)]
    lists.append(list(range(n)))
    M, B = build(lists)
    worst = max(checkpointed(lists, M, B, x, {0})[1]
                for x in range(0, n, max(1, n // 64)))
    return worst


def analytic(k, n, d=2):
    m = [0] * k
    m[k - 1] = n
    for i in range(k - 2, -1, -1):
        m[i] = n + m[i + 1] // d
    W = [(mi + 63) // 64 for mi in m[:-1]]
    S = [(w + 31) // 32 for w in W]
    st = sum(80 * w + 32 * s for w, s in zip(W, S))
    N, Mt = k * n, sum(m)
    return Mt / N, st / (Mt - n), st / N, 100 * (Mt - N) / N, m


def ckpt_extra(k, n, t, d=2):
    m = analytic(k, n, d)[4]
    return 32 * sum(m[i] - n for i in range(0, k, t)) / (k * n)


def example():
    lists = [[4, 13, 20, 31, 45, 52],
             [8, 15, 24, 36, 49, 57],
             [2, 17, 27, 33, 41, 60],
             [6, 11, 22, 29, 38, 47, 55, 63]]
    M, B = build(lists)
    for i in range(len(lists)):
        print(f"L{i+1} =", lists[i])
    for i in range(len(lists)):
        print(f"M{i+1} =", M[i])
        print(f"B{i+1} =", "".join(map(str, B[i])))
    x = 30
    print("forward x=30:", forward(lists, M, B, x))
    print("reverse x=30:", reverse(lists, B, x))
    print("truth:", truth(lists, x))
    for i in range(len(lists)):
        for p in range(len(M[i])):
            ch = chain(lists, B, i, p)
            if len(ch) > 2:
                print("chain", ch, M[i][p])
    print("ckpt {0} x=30:", checkpointed(lists, M, B, x, {0}))


def bsearch_cmp(L, x, lo, hi):
    c = 0
    while lo < hi:
        mid = (lo + hi) // 2
        c += 1
        if L[mid] < x:
            lo = mid + 1
        else:
            hi = mid
    return lo, c


def gap_stats(sizes, d, builds=20, queries=2000, seed=7):
    """Mean gap g_{i-1} and comparisons of reverse cascading on
    lists drawn uniformly from [0, 2^32), per level i-1."""
    rnd = random.Random(seed)
    k = len(sizes)
    gsum = [0.0] * k
    csum = [0.0] * k
    msum = [0.0] * k
    for _ in range(builds):
        lists = [sorted(rnd.randrange(2 ** 32) for _ in range(n))
                 for n in sizes]
        M, B = build(lists, d)
        ones = [[p for p, b in enumerate(Bi) if b] for Bi in B]
        for i in range(k):
            msum[i] += len(M[i])
        for _ in range(queries):
            x = rnd.randrange(2 ** 32)
            p = bisect.bisect_left(lists[-1], x)
            for i in range(k - 1, 0, -1):
                s = p // d
                o = ones[i - 1]
                a = o[s - 1] if s >= 1 else -1
                z = o[s] if s < len(o) else len(B[i - 1])
                lo, hi = a + 1 - s, z - s
                ell, c = bsearch_cmp(lists[i - 1], x, lo, hi)
                gsum[i - 1] += hi - lo
                csum[i - 1] += c
                p = ell + s
    t = builds * queries
    rows = []
    for j in range(k - 1):
        g, c = gsum[j] / t, csum[j] / t
        m = msum[j + 1] / builds
        est = (d + 1) * sizes[j] / (m + 2)
        bound = 2 * d * sizes[j] / (sizes[j + 1] + 2)
        rows.append((j + 1, g, est, bound, c))
    return rows


def gaps():
    print("level  mean_g  estimate  bound  mean_cmp  cmp_bound")
    for d in (2, 4, 8, 16, 64):
        rows = gap_stats([4000] * 6, d)
        g = sum(r[1] for r in rows) / len(rows)
        e = sum(r[2] for r in rows) / len(rows)
        b = sum(r[3] for r in rows) / len(rows)
        c = sum(r[4] for r in rows) / len(rows)
        cb = 1 + math.log2(1 + b)
        print("bal d=%-3d %7.3f %8.3f %6.3f %8.3f %9.3f"
              % (d, g, e, b, c, cb))
    for r in gap_stats([500, 4000, 1000, 8000, 250, 4000], 4):
        print("unbal d=4 L%d %7.3f %8.3f %6.3f %8.3f %9.3f"
              % (r[0], r[1], r[2], r[3], r[4],
                 1 + math.log2(1 + r[3])))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "example":
        example()
        sys.exit()
    if len(sys.argv) > 1 and sys.argv[1] == "gaps":
        gaps()
        sys.exit()
    test()
    for k in (4, 8, 16):
        print("adversary k=%d steps=%d" % (k, adversary(k, 2 ** k)))
    for d in (2, 4, 8):
        for k in (4, 8, 16, 32, 64, 128):
            r = analytic(k, 65536, d)
            print(d, k, "M/N=%.4f st/M=%.4f st/N=%.4f exp=%.2f"
                  % r[:4], "tot=%.2f" % (32 * (r[0] - 1) + r[2]))
    for t in (1, 2, 4, 8, 16, 128):
        print("ckpt k=128 t=%d extra key bits/N=%.3f"
              % (t, ckpt_extra(128, 65536, t)))
