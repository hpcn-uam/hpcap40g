#!/usr/bin/gnuplot

load 'gnuplot/config.gp'

set term cairolatex color size 4.7in,3in font ",10" dashed transparent
set output 'gnuplot/hpcapdd-nostorage.tex'

set title 'Capture rate'

set grid x y
set xlabel 'Frame size (bytes)'
set y2label 'Rate (Gbps)'
set ylabel 'Loss \%'

set y2tics
set ytics nomirror

set xtics 0,100,1500
set ytics
set mxtics 4
set mytics

set key bottom right

set style line 102 lc rgb '#A0A0A0' lt 3 lw 0.25
set style line 103 lc rgb '#A0A0A0' lt 3 lw 0.5 dt 2

set yrange [0:10]
set y2range [0:10]
set xrange [64:1500]

set pointintervalbox 0.5

fin = 'datasets/benchmark-20170210_1459-hpcapdd-nostore.dat'

plot \
	fin using 2:10 with linespoints ls 1 lw 3 title 'Lost \%', \
	fin using 2:12 with linespoints ls 2 lw 3 title 'Port Drop \%',  \
	fin using 2:($4 / 1000) with linespoints ls 3 lw 3 title 'Send rate' axes x1y2, \
	fin using 2:(8 * $6 * $2 / ($3 * 1000000) / 1000) with linespoints ls 4 axes x1y2 title 'Capture rate', \
	10000*x/(x+24)/1000 with lines ls 5 title 'Theoretical max rate' axes x1y2
