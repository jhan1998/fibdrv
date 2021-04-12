reset
set title "Fibonacci runtime"
set xlabel "F(N)"
set ylabel "nanosecond"
set terminal png font " Times_New_Roman,12 "
set output "after.png"
set xtics 0 ,10 ,100
set key left 

plot \
"plot.txt" using 1:2 with linespoints linewidth 2 title "iterate", \
"plot.txt" using 1:3 with linespoints linewidth 2 title "fast doubling", \
