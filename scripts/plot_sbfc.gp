# plot_sbfc.gp - Three-panel scaling plot for SBFC variants
reset

if (!exists("CSV_FILE")) CSV_FILE = "benchmark_all_variants.csv"
if (!exists("OUT_PDF"))  OUT_PDF  = "sbfc_scaling.pdf"
if (!exists("OUT_EPS"))  OUT_EPS  = "sbfc_scaling.eps"

set datafile separator ","

# --- 1. PDF output ---
set terminal pdfcairo enhanced color size 7in, 2.5in font "Helvetica,7"
set output OUT_PDF

set multiplot layout 1,3 margins 0.07, 0.985, 0.17, 0.80 spacing 0.08, 0.10

# Panel 1: Query latency (ns/query)
set title "Query Time vs. k (|L_i| = 65536)" font ",8"
set xlabel "Number of catalogs (k)"
set ylabel "Time per query (ns)"
set logscale x 2
set logscale y 10
set xtics (4, 8, 16, 32, 64, 128)
set grid ytics xtics lc rgb "#dddddd" lt 1
unset key

plot CSV_FILE every ::1 using 1:6  with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized SBFC", \
     CSV_FILE every ::1 using 1:10 with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only SBFC", \
     CSV_FILE every ::1 using 1:14 with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade"

# Panel 2: Total extra bits per input element (keys + bitvector/index)
unset logscale x
unset logscale y
set title "Total Extra Bits / Input Elem (N)" font ",8"
set xlabel "Number of catalogs (k)"
set ylabel "Total extra bits / N"
set xtics (4, 8, 16, 32, 64, 128)
set yrange [0:36]
unset key

plot CSV_FILE every ::1 using 1:5  with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized SBFC", \
     CSV_FILE every ::1 using 1:9  with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only SBFC", \
     CSV_FILE every ::1 using 1:13 with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade"

# Panel 3: Structural bitvector/index bits per input element only
set title "Structural Index Bits / Input Elem (N)" font ",8"
set xlabel "Number of catalogs (k)"
set ylabel "Structural bits / N"
set xtics (4, 8, 16, 32, 64, 128)
set yrange [0:3.2]
unset key

plot CSV_FILE every ::1 using 1:4  with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized (L1+L2)", \
     CSV_FILE every ::1 using 1:8  with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only (L1+L2)", \
     CSV_FILE every ::1 using 1:12 with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade (rank+sel)"

# Shared horizontal legend for all panels: an invisible full-width
# plot that only draws the key; afterwards restore the defaults.
set lmargin at screen 0.07; set rmargin at screen 0.985
set tmargin at screen 0.99; set bmargin at screen 0.90
unset border; unset tics; unset xlabel; unset ylabel; unset title
unset grid; unset logscale
set xrange [0:1]; set yrange [0:1]
set key center center horizontal maxrows 1 box opaque width 1
plot NaN with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized SBFC", \
     NaN with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only SBFC", \
     NaN with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade"
set border; set tics; set autoscale
set lmargin -1; set rmargin -1; set tmargin -1; set bmargin -1

unset multiplot
unset output

# --- 2. EPS output ---
set terminal postscript eps enhanced color size 7in, 2.5in font "Helvetica,7"
set output OUT_EPS

set multiplot layout 1,3 margins 0.07, 0.985, 0.17, 0.80 spacing 0.08, 0.10

set title "Query Time vs. k (|L_i| = 65536)" font ",8"
set xlabel "Number of catalogs (k)"
set ylabel "Time per query (ns)"
set logscale x 2
set logscale y 10
set xtics (4, 8, 16, 32, 64, 128)
set grid ytics xtics lc rgb "#dddddd" lt 1
unset key

plot CSV_FILE every ::1 using 1:6  with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized SBFC", \
     CSV_FILE every ::1 using 1:10 with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only SBFC", \
     CSV_FILE every ::1 using 1:14 with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade"

unset logscale x
unset logscale y
set title "Total Extra Bits / Input Elem (N)" font ",8"
set xlabel "Number of catalogs (k)"
set ylabel "Total extra bits / N"
set xtics (4, 8, 16, 32, 64, 128)
set yrange [0:36]
unset key

plot CSV_FILE every ::1 using 1:5  with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized SBFC", \
     CSV_FILE every ::1 using 1:9  with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only SBFC", \
     CSV_FILE every ::1 using 1:13 with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade"

set title "Structural Index Bits / Input Elem (N)" font ",8"
set xlabel "Number of catalogs (k)"
set ylabel "Structural bits / N"
set xtics (4, 8, 16, 32, 64, 128)
set yrange [0:3.2]
unset key

plot CSV_FILE every ::1 using 1:4  with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized (L1+L2)", \
     CSV_FILE every ::1 using 1:8  with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only (L1+L2)", \
     CSV_FILE every ::1 using 1:12 with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade (rank+sel)"

# Shared horizontal legend for all panels: an invisible full-width
# plot that only draws the key; afterwards restore the defaults.
set lmargin at screen 0.07; set rmargin at screen 0.985
set tmargin at screen 0.99; set bmargin at screen 0.90
unset border; unset tics; unset xlabel; unset ylabel; unset title
unset grid; unset logscale
set xrange [0:1]; set yrange [0:1]
set key center center horizontal maxrows 1 box opaque width 1
plot NaN with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized SBFC", \
     NaN with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only SBFC", \
     NaN with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade"
set border; set tics; set autoscale
set lmargin -1; set rmargin -1; set tmargin -1; set bmargin -1

unset multiplot
unset output
