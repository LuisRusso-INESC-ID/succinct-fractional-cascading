# Succinct Fractional Cascading

Implementation, tests and benchmarks for *Succinct Bit-Cascaded
Catalogs* (SBFC): pointer-free fractional cascading over sorted
integer lists. Bridge pointers are replaced by one origin bit per
augmented entry plus a rank/select index, optionally compressed
with Elias-Fano or RRR.

## Layout

```
src/sbfc.c           all variants, one C11 source
src/test_bv.c        rank/select unit test (includes sbfc.c)
scripts/             gnuplot scripts and the Python reference model
results/             benchmark CSVs, figures, counter table
Makefile             build, test, benchmark and plot targets
```

## Build and test

```sh
make            # the three benchmark binaries and test_bv
make test       # unit test plus randomized end-to-end checks
```

`make` builds three binaries from `src/sbfc.c`:

| Binary               | Variant                                    |
|----------------------|--------------------------------------------|
| `succinct_fc_2level` | materialized SBFC (checkpoint every level) |
| `strict_m1_sbfc`     | only the top catalog stores its samples    |
| `rev_cascade_sbfc`   | reverse cascading, no duplicated keys      |

## Usage

```
prog [--cpu c] [--csv] [--stride d] [--ckpt t]
     [--bv plain|ef|rrr|auto] [--pmu] [--verify]
     [--seed s] [--range r] [--vary]
     [--dist uniform|zipf|cluster] [--baseline] [--gapstats]
     k n queries
```

- `k n queries`: number of lists, keys per list, random queries.
- `--stride d`: sample every d-th element (default 2).
- `--ckpt t`: store sampled keys at levels 1, 1+t, 1+2t, ...;
  `0` stores none.
- `--bv`: origin-bit encoding: plain bits with a two-level rank
  directory, Elias-Fano, RRR-63, or the smallest per level.
- `--pmu`: read hardware counters (cycles, instructions, cache
  and branch events) with `perf_event_open`, around the query
  loop only. No `perf` tool is needed. Use `--cpu` on hybrid
  CPUs.
- `--verify`: check every answer against independent binary
  searches.
- `--dist`: input distribution: uniform values (default), Zipf
  list lengths, or values in narrow clusters (queried near the
  data).
- `--baseline`: answer queries with k independent binary searches.
- `--gapstats` (reverse variant only): measure the gaps and
  comparisons of every restricted binary search in an untimed
  pass, and compare them with the expected-cost bound of the
  paper.
- `--cpu c`: pin to core `c`. `--csv`: one CSV line of output.

Without `--csv` the output also lists the size of every bit-string
encoding and the entropy lower bound.

## Benchmarks

```sh
make CPU_CORE=2 bench-all        # everything below
make CPU_CORE=2 plots            # k sweep -> results/sbfc_scaling.*
make CPU_CORE=2 sweep-size       # list-length sweep
make CPU_CORE=2 perf-tex         # hardware-counter table
make CPU_CORE=2 dist-table       # uniform, Zipf, clustered inputs
make gap-table                   # reverse-cascading gaps vs bound
make CPU_CORE=2 SBFC_OPTS="--stride 4 --bv ef" csv
```

`SBFC_OPTS` is passed to every benchmark run. The counters need
`/proc/sys/kernel/perf_event_paranoid` to be 2 or lower.

The paper lives in a separate Overleaf project. With its clone in
`../overleaf`, `make paper-assets` copies the current figures and
counter table into its `figures/` folder
(`OVERLEAF_DIR=...` to change the path).

## Reference model

```sh
python3 scripts/sbfc_check.py           # randomized checks, tables
python3 scripts/sbfc_check.py example   # the paper's running example
python3 scripts/sbfc_check.py gaps      # reverse-cascading gaps
```

## Cleaning

`make clean` removes build products. `make distclean` also deletes
the generated results in `results/`.

## License

MIT, see `LICENSE`.
