#!/usr/bin/gnuplot

load 'gnuplot/config.gp'

set term cairolatex color size 4.7in,3in font ",10" dashed transparent
set output 'gnuplot/duplicate-rates.tex'

set title 'Losses on duplicate detection'

set grid x y
set xlabel 'Frame size (bytes)'
set ylabel 'Loss \%'


set xtics 0,10,150
set ytics
set mxtics 4
set mytics

set key top right

set style line 102 lc rgb '#A0A0A0' lt 3 lw 0.25
set style line 103 lc rgb '#A0A0A0' lt 3 lw 0.5 dt 2

set yrange [0:40]
set xrange [64:150]

set pointintervalbox 0.5

fin_dr2 = 'datasets/benchmark-20170210_1846_duprate2.dat'
fin_dr5 = 'datasets/benchmark-20170210_1846_duprate5.dat'
fin_dr20 = 'datasets/benchmark-20170210_1846_duprate20.dat'
fin_dr100 = 'datasets/benchmark-20170210_1846_duprate100.dat'
fin_dr300 = 'datasets/benchmark-20170210_1846_duprate300.dat'
fin_dr1000 = 'datasets/benchmark-20170210_1846_duprate1000.dat'


plot \
	fin_dr2 using 2:10 with linespoints ls 1 lw 3 title 'Lost \% ($\lambda$ = 2)', \
	fin_dr5 using 2:10 with linespoints ls 2 lw 3 title 'Lost \% ($\lambda$ = 5)', \
	fin_dr20 using 2:10 with linespoints ls 3 lw 3 title 'Lost \% ($\lambda$ = 20)', \
	fin_dr300 using 2:10 with linespoints ls 4 lw 3 title 'Lost \% ($\lambda$ = 300)', \
	fin_dr1000 using 2:10 with linespoints ls 5 lw 3 title 'Lost \% ($\lambda$ = 1000)'
