# plot_size_sweep.gp - Per-list size sweep (k = 16) for cache-transition inspection
reset

if (!exists("CSV_FILE")) CSV_FILE = "benchmark_size_sweep.csv"
if (!exists("OUT_PDF"))  OUT_PDF  = "sbfc_size_sweep.pdf"
if (!exists("OUT_EPS"))  OUT_EPS  = "sbfc_size_sweep.eps"

set datafile separator ","

# --- 1. PDF output ---
set terminal pdfcairo enhanced color size 7in, 3.0in font "Helvetica,9"
set output OUT_PDF

set multiplot layout 1,2 margins 0.085, 0.98, 0.18, 0.80 spacing 0.10, 0.12

# Left panel: Query latency vs per-list length |L_i| (log2 x, log10 y)
set title "Query Latency vs. Per-List Size |L_i| (k = 16)" font ",10"
set xlabel "Elements per list |L_i|"
set ylabel "Time per query (ns)"
set logscale x 2
set logscale y 10
set xtics ("2^{10}" 1024, "2^{12}" 4096, "2^{14}" 16384, "2^{16}" 65536, \
           "2^{18}" 262144, "2^{20}" 1048576)
set grid ytics xtics lc rgb "#dddddd" lt 1
unset key

plot CSV_FILE every ::1 using 1:7  with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized SBFC", \
     CSV_FILE every ::1 using 1:11 with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only SBFC", \
     CSV_FILE every ::1 using 1:15 with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade"

# Right panel: Structural bits per input element vs per-list length |L_i|
unset logscale y
set title "Structural Index Bits / Input Elem vs. |L_i| (k = 16)" font ",10"
set xlabel "Elements per list |L_i|"
set ylabel "Structural bits / N"
set yrange [0:3.2]
unset key

plot CSV_FILE every ::1 using 1:5  with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized (L1+L2)", \
     CSV_FILE every ::1 using 1:9  with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only (L1+L2)", \
     CSV_FILE every ::1 using 1:13 with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade (rank+sel)"

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
set terminal postscript eps enhanced color size 7in, 3.0in font "Helvetica,9"
set output OUT_EPS

set multiplot layout 1,2 margins 0.085, 0.98, 0.18, 0.80 spacing 0.10, 0.12

set title "Query Latency vs. Per-List Size |L_i| (k = 16)" font ",10"
set xlabel "Elements per list |L_i|"
set ylabel "Time per query (ns)"
set logscale x 2
set logscale y 10
set xtics ("2^{10}" 1024, "2^{12}" 4096, "2^{14}" 16384, "2^{16}" 65536, \
           "2^{18}" 262144, "2^{20}" 1048576)
set grid ytics xtics lc rgb "#dddddd" lt 1
unset key

plot CSV_FILE every ::1 using 1:7  with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized SBFC", \
     CSV_FILE every ::1 using 1:11 with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only SBFC", \
     CSV_FILE every ::1 using 1:15 with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade"

unset logscale y
set title "Structural Index Bits / Input Elem vs. |L_i| (k = 16)" font ",10"
set xlabel "Elements per list |L_i|"
set ylabel "Structural bits / N"
set yrange [0:3.2]
unset key

plot CSV_FILE every ::1 using 1:5  with linespoints lw 1.4 pt 7 ps 0.7 lc rgb "#1f77b4" title "Materialized (L1+L2)", \
     CSV_FILE every ::1 using 1:9  with linespoints lw 1.4 pt 5 ps 0.7 lc rgb "#ff7f0e" title "M1-Only (L1+L2)", \
     CSV_FILE every ::1 using 1:13 with linespoints lw 1.4 pt 9 ps 0.7 lc rgb "#2ca02c" title "Reverse-Cascade (rank+sel)"

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
