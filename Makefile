CC       ?= cc
CFLAGS   ?= -O3 -march=native -std=c11 -Wall -Wextra
GNUPLOT  ?= gnuplot
TASKSET  ?= taskset
CPU_CORE ?= 2

SRC      := src/sbfc.c
RES      := results
# Overleaf clone that receives the figures (make paper-assets)
OVERLEAF_DIR ?= ../overleaf
# extra flags for every run, e.g. SBFC_OPTS="--stride 4 --ckpt 8"
SBFC_OPTS ?=

BIN_MAT  := succinct_fc_2level
BIN_M1   := strict_m1_sbfc
BIN_REV  := rev_cascade_sbfc

CSV_MAT  := $(RES)/bench_mat.csv
CSV_M1   := $(RES)/bench_m1.csv
CSV_REV  := $(RES)/bench_rev.csv
CSV_ALL  := $(RES)/benchmark_all_variants.csv

K_LIST   := 4 8 16 32 64 128
LIST_N   := 65536
QUERIES  := 200000

SWEEP_K       := 16
SWEEP_N_LIST  := 1024 2048 4096 8192 16384 32768 65536 131072 262144 524288 1048576
SWEEP_QUERIES := 100000
CSV_SWEEP     := $(RES)/benchmark_size_sweep.csv
SWEEP_GP      := scripts/plot_size_sweep.gp
PDF_SWEEP     := $(RES)/sbfc_size_sweep.pdf
EPS_SWEEP     := $(RES)/sbfc_size_sweep.eps

PERF_CSV    := $(RES)/perf_pinned.csv
PERF_TEX    := $(RES)/perf_pinned_table.tex
PERF_CORE_K := 16
PERF_REPS   := 5

SMOKE_K       := 4
SMOKE_N       := 1024
SMOKE_QUERIES := 2000
SMOKE_CSV     := smoke_bench.csv

PLOT_GP  := scripts/plot_sbfc.gp
PDF_PLOT := $(RES)/sbfc_scaling.pdf
EPS_PLOT := $(RES)/sbfc_scaling.eps

.PHONY: all bins test csv plots sweep-size sweep-plots perf-pinned perf-tex bench-all smoke-test dist-table paper-assets clean distclean

all: bins test_bv

bins: $(BIN_MAT) $(BIN_M1) $(BIN_REV)

$(BIN_MAT): $(SRC)
	$(CC) $(CFLAGS) -DVARIANT=0 $< -o $@ -lm

$(BIN_M1): $(SRC)
	$(CC) $(CFLAGS) -DVARIANT=1 $< -o $@ -lm

$(BIN_REV): $(SRC)
	$(CC) $(CFLAGS) -DVARIANT=2 $< -o $@ -lm

test_bv: src/test_bv.c $(SRC)
	$(CC) $(CFLAGS) $< -o $@ -lm

test: bins test_bv
	@./test_bv
	@for bin in $(BIN_MAT) $(BIN_M1) $(BIN_REV); do \
	 for bv in plain ef rrr auto; do \
	  for d in 2 3 4 8 32; do \
	    for t in 0 1 2 3 1000; do \
	      for k in 1 2 5 9 17; do \
	        ./$$bin --verify --vary --range 60 --stride $$d \
	          --ckpt $$t --bv $$bv --seed $$k$$d$$t $$k 40 3000 \
	          > /dev/null || { echo "FAIL: $$bin d=$$d t=$$t" \
	          "k=$$k bv=$$bv"; exit 1; }; \
	      done; \
	    done; \
	    ./$$bin --verify --stride $$d --bv $$bv 16 20000 20000 \
	      > /dev/null || { echo "FAIL: $$bin d=$$d large"; exit 1; }; \
	  done; \
	  echo "OK  : $$bin --bv $$bv"; \
	 done; \
	done
	@for bin in $(BIN_MAT) $(BIN_M1) $(BIN_REV); do \
	  for dist in zipf cluster; do \
	    for bv in plain ef rrr; do \
	      for k in 1 3 9 17; do \
	        for d in 2 4; do \
	          ./$$bin --verify --vary --range 60 --dist $$dist \
	            --bv $$bv --stride $$d --seed $$k$$d $$k 40 3000 \
	            > /dev/null || { echo "FAIL: $$bin dist=$$dist" \
	            "bv=$$bv k=$$k d=$$d"; exit 1; }; \
	        done; \
	      done; \
	    done; \
	    ./$$bin --verify --dist $$dist 16 20000 20000 > /dev/null \
	      || { echo "FAIL: $$bin dist=$$dist large"; exit 1; }; \
	  done; \
	  echo "OK  : $$bin --dist zipf, cluster"; \
	done

$(CSV_MAT): $(BIN_MAT)
	@echo "k,N,extra_key_bits_per_in,struct_bits_per_in,total_extra_bits_per_in,ns_per_query" > $@
	@for k in $(K_LIST); do \
		./$(BIN_MAT) --cpu $(CPU_CORE) $(SBFC_OPTS) --csv $$k $(LIST_N) $(QUERIES) >> $@; \
	done

$(CSV_M1): $(BIN_M1)
	@echo "k,N,extra_key_bits_per_in,struct_bits_per_in,total_extra_bits_per_in,ns_per_query" > $@
	@for k in $(K_LIST); do \
		./$(BIN_M1) --cpu $(CPU_CORE) $(SBFC_OPTS) --csv $$k $(LIST_N) $(QUERIES) >> $@; \
	done

$(CSV_REV): $(BIN_REV)
	@echo "k,N,extra_key_bits_per_in,struct_bits_per_in,total_extra_bits_per_in,ns_per_query" > $@
	@for k in $(K_LIST); do \
		./$(BIN_REV) --cpu $(CPU_CORE) $(SBFC_OPTS) --csv $$k $(LIST_N) $(QUERIES) >> $@; \
	done

$(CSV_ALL): $(CSV_MAT) $(CSV_M1) $(CSV_REV)
	@echo "k,N,mat_extra_key,mat_struct,mat_total_extra,mat_ns,m1_extra_key,m1_struct,m1_total_extra,m1_ns,rev_extra_key,rev_struct,rev_total_extra,rev_ns" > $@
	@awk -F',' 'FNR==1 { next } { k=$$1; n=$$2; if (FILENAME=="$(CSV_MAT)") { mat_ek[k]=$$3; mat_st[k]=$$4; mat_tot[k]=$$5; mat_ns[k]=$$6; ks[k]=n; } else if (FILENAME=="$(CSV_M1)") { m1_ek[k]=$$3; m1_st[k]=$$4; m1_tot[k]=$$5; m1_ns[k]=$$6; } else if (FILENAME=="$(CSV_REV)") { rev_ek[k]=$$3; rev_st[k]=$$4; rev_tot[k]=$$5; rev_ns[k]=$$6; } } END { split("$(K_LIST)", arr, " "); for (i in arr) { k=arr[i]; printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", k, ks[k], mat_ek[k], mat_st[k], mat_tot[k], mat_ns[k], m1_ek[k], m1_st[k], m1_tot[k], m1_ns[k], rev_ek[k], rev_st[k], rev_tot[k], rev_ns[k]; } }' $(CSV_MAT) $(CSV_M1) $(CSV_REV) >> $@
	@echo "Wrote $(CSV_ALL)"

csv: $(CSV_ALL)

$(CSV_SWEEP): $(BIN_MAT) $(BIN_M1) $(BIN_REV)
	@echo "per_list_n,k,N,mat_extra_key,mat_struct,mat_total_extra,mat_ns,m1_extra_key,m1_struct,m1_total_extra,m1_ns,rev_extra_key,rev_struct,rev_total_extra,rev_ns" > $@
	@for n in $(SWEEP_N_LIST); do \
		m_row=$$(./$(BIN_MAT) --cpu $(CPU_CORE) $(SBFC_OPTS) --csv $(SWEEP_K) $$n $(SWEEP_QUERIES)); \
		m1_row=$$(./$(BIN_M1) --cpu $(CPU_CORE) $(SBFC_OPTS) --csv $(SWEEP_K) $$n $(SWEEP_QUERIES)); \
		rev_row=$$(./$(BIN_REV) --cpu $(CPU_CORE) $(SBFC_OPTS) --csv $(SWEEP_K) $$n $(SWEEP_QUERIES)); \
		total_n=$$(echo "$$m_row" | cut -d',' -f2); \
		m_ek=$$(echo "$$m_row" | cut -d',' -f3); \
		m_st=$$(echo "$$m_row" | cut -d',' -f4); \
		m_tot=$$(echo "$$m_row" | cut -d',' -f5); \
		m_ns=$$(echo "$$m_row" | cut -d',' -f6); \
		m1_ek=$$(echo "$$m1_row" | cut -d',' -f3); \
		m1_st=$$(echo "$$m1_row" | cut -d',' -f4); \
		m1_tot=$$(echo "$$m1_row" | cut -d',' -f5); \
		m1_ns=$$(echo "$$m1_row" | cut -d',' -f6); \
		r_ek=$$(echo "$$rev_row" | cut -d',' -f3); \
		r_st=$$(echo "$$rev_row" | cut -d',' -f4); \
		r_tot=$$(echo "$$rev_row" | cut -d',' -f5); \
		r_ns=$$(echo "$$rev_row" | cut -d',' -f6); \
		printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n" \
			"$$n" "$(SWEEP_K)" "$$total_n" \
			"$$m_ek" "$$m_st" "$$m_tot" "$$m_ns" \
			"$$m1_ek" "$$m1_st" "$$m1_tot" "$$m1_ns" \
			"$$r_ek" "$$r_st" "$$r_tot" "$$r_ns" >> $@; \
	done
	@echo "Wrote $(CSV_SWEEP)"

$(PDF_SWEEP): $(CSV_SWEEP) $(SWEEP_GP)
	$(GNUPLOT) -e "OUT_PDF='$(PDF_SWEEP)'; OUT_EPS='$(EPS_SWEEP)'; CSV_FILE='$(CSV_SWEEP)'" $(SWEEP_GP)

$(EPS_SWEEP): $(PDF_SWEEP)
	@test -f $(EPS_SWEEP)

sweep-plots: $(PDF_SWEEP) $(EPS_SWEEP)

sweep-size: sweep-plots

perf-pinned: bins
	@for bin in $(BIN_MAT) $(BIN_M1) $(BIN_REV); do \
		echo "=== $$bin (--cpu $(CPU_CORE)) ==="; \
		./$$bin --cpu $(CPU_CORE) $(SBFC_OPTS) --pmu \
			$(PERF_CORE_K) $(LIST_N) $(QUERIES) \
			| sed -n '/ns per query/,/branch miss/p'; \
	done

# Hardware counters via perf_event_open (sbfc.c --pmu), counted
# around the query loop only; no perf tool needed.
$(PERF_CSV): bins
	@echo "variant,reps,cycles_q,instr_q,ipc_mean,ipc_sd,llc_miss_pct,llc_miss_q,br_miss_pct,ns_mean,ns_sd" > $@
	@for pair in "Materialized SBFC:$(BIN_MAT)" "M1-Only SBFC:$(BIN_M1)" "Reverse-Cascade:$(BIN_REV)"; do \
		label="$${pair%%:*}"; \
		bin="$${pair#*:}"; \
		acc_file="$$(mktemp)"; \
		rep=1; \
		while [ "$$rep" -le "$(PERF_REPS)" ]; do \
			./$$bin --cpu $(CPU_CORE) $(SBFC_OPTS) --csv --pmu \
				$(PERF_CORE_K) $(LIST_N) $(QUERIES) >> "$$acc_file" \
				|| { rm -f "$$acc_file"; exit 1; }; \
			rep=$$((rep + 1)); \
		done; \
		awk -F',' -v v="$$label" -v q="$(QUERIES)" ' \
			function add(st, x,    d) { \
				st["n"]++; d = x - st["m"]; \
				st["m"] += d / st["n"]; \
				st["s"] += d * (x - st["m"]); \
			} \
			function sd(st) { \
				return st["n"] > 1 ? sqrt(st["s"] / (st["n"] - 1)) : 0; \
			} \
			NF == 12 { \
				add(ns, $$6); add(cyc, $$7 / q); add(ins, $$8 / q); \
				add(ipc, $$7 > 0 ? $$8 / $$7 : 0); \
				add(llc, $$9 > 0 ? 100 * $$10 / $$9 : 0); \
				add(llq, $$10 / q); \
				add(br, $$11 > 0 ? 100 * $$12 / $$11 : 0); \
			} \
			END { \
				printf "%s,%d,%.1f,%.1f,%.4f,%.4f,%.3f,%.3f,%.3f,%.2f,%.2f\n", \
					v, ns["n"], cyc["m"], ins["m"], ipc["m"], sd(ipc), \
					llc["m"], llq["m"], br["m"], ns["m"], sd(ns); \
			}' "$$acc_file" >> $@; \
		rm -f "$$acc_file"; \
	done
	@echo "Wrote $(PERF_CSV) (mean over $(PERF_REPS) reps)"

$(PERF_TEX): $(PERF_CSV)
	@awk -F',' ' \
		function f(x, d) { return sprintf("%." d "f", x); } \
		BEGIN { \
			print "% Auto-generated by make perf-tex"; \
			print "\\begin{table}[t]"; \
			print "\\centering"; \
			print "\\caption{Hardware counters over the query loop only,"; \
			print "read with \\texttt{perf\\_event\\_open}: mean over"; \
			print "$(PERF_REPS) runs on core $(CPU_CORE), $$k = $(PERF_CORE_K)$$,"; \
			print "$$n_i = $(LIST_N)$$, $(QUERIES) random queries."; \
			print "LLC: last-level cache.}"; \
			print "\\label{tab:perf-pinned-auto}"; \
			print "\\small"; \
			print "\\setlength{\\tabcolsep}{4pt}"; \
			print "\\begin{adjustbox}{max width=\\linewidth}"; \
			print "\\begin{tabular}{@{}lrrcrrrr@{}}"; \
			print "\\toprule"; \
			print "Variant & Cycl./q & Instr./q & IPC &"; \
			print "LLC miss (\\%) & LLC miss/q & Br.\\ miss (\\%) &"; \
			print "ns/q \\\\"; \
			print "\\midrule"; \
		} \
		NR > 1 { \
			print $$1 " & " f($$3, 0) " & " f($$4, 0) " & $$" \
				f($$5, 2) " \\pm " f($$6, 2) "$$ &"; \
			print "  " f($$7, 2) " & " f($$8, 2) " & " f($$9, 2) \
				" & " f($$10, 0) " \\\\"; \
		} \
		END { \
			print "\\bottomrule"; \
			print "\\end{tabular}"; \
			print "\\end{adjustbox}"; \
			print "\\end{table}"; \
		}' $(PERF_CSV) > $@
	@echo "Wrote $(PERF_TEX)"

perf-tex: $(PERF_TEX)

smoke-test: bins
	@rm -f $(SMOKE_CSV)
	@echo "k,N,extra_key_bits_per_in,struct_bits_per_in,total_extra_bits_per_in,ns_per_query" > $(SMOKE_CSV)
	@for bin in $(BIN_MAT) $(BIN_M1) $(BIN_REV); do \
		line=$$(./$$bin --cpu $(CPU_CORE) $(SBFC_OPTS) --csv $(SMOKE_K) $(SMOKE_N) $(SMOKE_QUERIES)); \
		count=$$(printf '%s\n' "$$line" | awk -F',' 'NF == 6 && $$1 ~ /^[0-9]+$$/ && $$2 ~ /^[0-9]+$$/ { print NF }'); \
		if [ "$$count" != "6" ]; then \
			echo "FAIL: $$bin produced unexpected CSV line: '$$line'" >&2; \
			rm -f $(SMOKE_CSV); \
			exit 1; \
		fi; \
		printf '%s\n' "$$line" >> $(SMOKE_CSV); \
		printf 'OK  : %-20s -> %s\n' "$$bin" "$$line"; \
	done
	@rm -f $(SMOKE_CSV)

bench-all:
	$(MAKE) smoke-test
	$(MAKE) csv
	$(MAKE) plots
	$(MAKE) sweep-size
	$(MAKE) perf-tex

plots: $(PDF_PLOT) $(EPS_PLOT)

$(PDF_PLOT): $(CSV_ALL) $(PLOT_GP)
	$(GNUPLOT) -e "OUT_PDF='$(PDF_PLOT)'; OUT_EPS='$(EPS_PLOT)'; CSV_FILE='$(CSV_ALL)'" $(PLOT_GP)

$(EPS_PLOT): $(PDF_PLOT)
	@test -f $(EPS_PLOT)

# Non-uniform inputs (--dist): space and median time of 3 runs.
CSV_DIST   := $(RES)/benchmark_distributions.csv
DIST_K     := 128
DIST_RUNS  := 3
DIST_CONFS := "Materialized d=2|$(BIN_MAT)|--stride 2" \
	"M1-only d=2|$(BIN_M1)|--stride 2" \
	"Reverse d=2|$(BIN_REV)|--stride 2" \
	"Materialized EF d=16|$(BIN_MAT)|--stride 16 --bv ef" \
	"Reverse EF d=16|$(BIN_REV)|--stride 16 --bv ef" \
	"Independent searches|$(BIN_REV)|--baseline"

dist-table: bins
	@echo "dist,config,N,total_extra_bits_per_in,ns_median" > $(CSV_DIST)
	@for dist in uniform zipf cluster; do \
		for conf in $(DIST_CONFS); do \
			label="$${conf%%|*}"; rest="$${conf#*|}"; \
			bin="$${rest%%|*}"; opts="$${rest#*|}"; \
			r=1; out=""; \
			while [ "$$r" -le "$(DIST_RUNS)" ]; do \
				out="$$out$$(./$$bin --cpu $(CPU_CORE) --csv --dist $$dist \
					$$opts $(DIST_K) $(LIST_N) $(SWEEP_QUERIES))\n"; \
				r=$$((r + 1)); \
			done; \
			printf "$$out" | sort -t, -k6,6g | awk -F, -v d="$$dist" \
				-v c="$$label" -v m="$$(( ($(DIST_RUNS) + 1) / 2 ))" \
				'NR == m { printf "%s,%s,%s,%s,%s\n", d, c, $$2, $$5, $$6 }' \
				>> $(CSV_DIST); \
		done; \
	done
	@echo "Wrote $(CSV_DIST)"

# Copy the current figures and counter table into the Overleaf
# clone (run make bench-all first to refresh them).
paper-assets:
	mkdir -p $(OVERLEAF_DIR)/figures
	cp $(PDF_PLOT) $(PDF_SWEEP) $(PERF_TEX) $(OVERLEAF_DIR)/figures/

# Remove build products only; results/ holds committed data.
clean:
	rm -f $(BIN_MAT) $(BIN_M1) $(BIN_REV) test_bv $(SMOKE_CSV)

# Also remove all generated results.
distclean: clean
	rm -f $(CSV_MAT) $(CSV_M1) $(CSV_REV) $(CSV_ALL) $(CSV_SWEEP) \
	      $(PDF_SWEEP) $(EPS_SWEEP) $(PERF_CSV) $(PERF_TEX) \
	      $(PDF_PLOT) $(EPS_PLOT) $(CSV_DIST)
