reset
set title "kernel space - user space"
set xlabel ""
set ylabel "nanosecond"
set terminal png font " Times_New_Roman,12 "
set output "k_u.png"
set xtics 0 ,10 ,100
set key left 

plot \
"plot.txt" using 1:2 with linespoints linewidth 2 title "user to kernel", \
"plot.txt" using 1:3 with linespoints linewidth 2 title "kernel to user", \
