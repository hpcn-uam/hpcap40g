# line styles for ColorBrewer Dark2
# for use with qualitative/categorical data
# provides 8 dark colors based on Set2
# compatible with gnuplot >=4.2
# author: Anna Schneider

# line styles
set style line 1 lt 1 lc rgb '#1B9E77' pt 7 pi -1 ps 0.2 # dark teal
set style line 2 lt 1 lc rgb '#D95F02' pt 7 pi -1 ps 0.2 # dark orange
set style line 3 lt 1 lc rgb '#7570B3' pt 7 pi -1 ps 0.2 # dark lilac
set style line 4 lt 1 lc rgb '#E7298A' pt 7 pi -1 ps 0.2 # dark magenta
set style line 5 lt 1 lc rgb '#66A61E' pt 7 pi -1 ps 0.2 # dark lime green
set style line 6 lt 1 lc rgb '#E6AB02' pt 7 pi -1 ps 0.2 # dark banana
set style line 7 lt 1 lc rgb '#A6761D' pt 7 pi -1 ps 0.2 # dark tan
set style line 8 lt 1 lc rgb '#666666' pt 7 pi -1 ps 0.2 # dark gray

set style line 11 lt 2 pt 7 pi -1 ps 0.2 dashtype 2 lc rgb '#1B9E77' # dark teal
set style line 12 lt 2 pt 7 pi -1 ps 0.2 dashtype 2 lc rgb '#D95F02' # dark orange
set style line 13 lt 2 pt 7 pi -1 ps 0.2 dashtype 2 lc rgb '#7570B3' # dark lilac
set style line 14 lt 2 pt 7 pi -1 ps 0.2 dashtype 2 lc rgb '#E7298A' # dark magenta
set style line 15 lt 2 pt 7 pi -1 ps 0.2 dashtype 2 lc rgb '#66A61E' # dark lime green
set style line 16 lt 2 pt 7 pi -1 ps 0.2 dashtype 2 lc rgb '#E6AB02' # dark banana
set style line 17 lt 2 pt 7 pi -1 ps 0.2 dashtype 2 lc rgb '#A6761D' # dark tan
set style line 18 lt 2 pt 7 pi -1 ps 0.2 dashtype 2 lc rgb '#666666' # dark gray

# palette
set palette maxcolors 8
set palette defined ( 0 '#1B9E77',\
    	    	      1 '#D95F02',\
		      2 '#7570B3',\
		      3 '#E7298A',\
		      4 '#66A61E',\
		      5 '#E6AB02',\
		      6 '#A6761D',\
		      7 '#666666' )


### Tics

set xtics
set ytics
set mxtics
set mytics

### Grids

set style line 102 lc rgb '#808080' lt 3 lw 1
set style line 103 lc rgb '#A0A0A0' dashtype 3 lw 0.5

set grid xtics mxtics ls 102, ls 103
set grid ytics mytics ls 102, ls 103

## Functions
max(x,y) = (x < y) ? y : x
min(x,y) = (x < y) ? x : y

