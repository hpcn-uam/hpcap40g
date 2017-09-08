#!/bin/bash

base="/almacenamiento"
core=4

ls -rt1 $base |
	while read dir
	do
		ls -rt1 ${base}/${dir} |
			while read rawfile
			do
				pcapfile=$( echo $rawfile | awk -v FS="." '{print $1}')
				
				#echo "./raw2pcap ${base}/${dir}/${rawfile} prueba"
				echo "ionice -c 3 taskset -c $core /raw2pcap ${base}/${dir}/${rawfile} prueba"
				ionice -c 3 taskset -c $core ./raw2pcap ${base}/${dir}/${rawfile} prueba
				rm -f prueba_0.pcap
			done			
	done
