#!/bin/bash

source scripts/lib.bash

#program="monitor_flujos"
program="mgmon"
#diskdir="almacenamiento"
diskdir="disco01"

base="$(read_value_param basedir)/data"
linea="$(date +%s) "
ps -A -o pid,tid,class,rtprio,ni,pri,pcpu,stat,wchan:14,comm,psr,pmem > ${base}/auxps
memdatos=$(free | grep "Mem:" | awk '{print $2 " " $3 " " $4 " " $7}')
discos=$(df -h | grep $diskdir | awk '{print $5}')
n=$(cat ${base}/auxps | grep $program | wc -l)
if [ $n -eq 0 ]
then
	mem="0.0"
else
	mem=$(cat ${base}/auxps | grep $program | awk '{print $12}')
fi
datos=$(cat ${base}/auxps |
	awk '\
	{\
		if(NR!=1)\
		{\
			proc=$11;\
			tabla[proc]++;\
		}\
	}\
	\
	END\
	{\
		total=0;\
		cadena="";
		for(i=0;i<12;i++)\
		{\
			total+=tabla[i];\
			cadena=sprintf("%s %d",cadena,tabla[i]);\
		}\
		print cadena;\
	}')
linea="$linea D:$discos $datos $memdatos $mem"

filename="${base}/$(date +%Y-%V)/cpus"
touch $filename
echo $linea >> $filename
